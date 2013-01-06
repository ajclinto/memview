
/*--------------------------------------------------------------------*/
/*--- A tool to output memory traces                     mv_main.c ---*/
/*--------------------------------------------------------------------*/

#include "mv_ipc.h"
#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"     // VG_(fnptr_to_fnentry)
#include "pub_tool_vki.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_stacktrace.h"
#include "coregrind/pub_core_aspacemgr.h"

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

static int		 clo_pipe = 0;
static Bool		 clo_trace_instrs = False;
static const char	*clo_shared_mem = 0;

static Bool mv_process_cmd_line_option(const HChar* arg)
{
    if VG_INT_CLO(arg, "--pipe",		clo_pipe) {}
    else if VG_STR_CLO(arg, "--shared-mem",	clo_shared_mem) {}
    else if VG_BOOL_CLO(arg, "--trace-instrs",	clo_trace_instrs) {}
    else
	return VG_(replacement_malloc_process_cmd_line_option)(arg);

    return True;
}

static void mv_print_usage(void)
{  
    VG_(printf)(
	    "    --pipe=<fd>                pipe to fd [2]\n"
	    "    --shared-mem=<file>        shared memory output file [""]\n"
	    "    --trace-instrs=yes         trace instruction memory [no]\n"
	    );
}

static void mv_print_debug_usage(void)
{  
    VG_(printf)(
	    "    (none)\n"
	    );
}

/*------------------------------------------------------------*/
/*--- Memory Trace IPC					      */
/*------------------------------------------------------------*/

static TraceBlock	*theBlock = 0;

// Data for pipe
static TraceBlock	 theBlockData;
// Data for shm
static SharedData	*theSharedData = 0;
static int		 theIdx = 0;

typedef unsigned long long uint64;
static uint64		 theTotalEvents = 0;

static void flush_data(void)
{
    //VG_(get_and_pp_StackTrace)(VG_(get_running_tid)(), 8);
    if (clo_shared_mem)
    {
	// Post the full block
	__sync_bool_compare_and_swap(&theBlock->myRSem, 0, 1);

	theIdx++;
	if (theIdx == theBlockCount)
	    theIdx = 0;

	theBlock = &theSharedData->myBlocks[theIdx];

	// Wait until we can write to the new theBlock
	while (!__sync_bool_compare_and_swap(&theBlock->myWSem, 1, 0))
	    ;
    }
    else if (clo_pipe)
    {
	VG_(write)(clo_pipe, &theBlockData, sizeof(theBlockData));
    }
    theTotalEvents += theBlock->myEntries;
    theBlock->myEntries = 0;
}

static inline void put_data(Addr addr, uint64 type, uint64 size)
{
    if (theBlock->myEntries >= theBlockSize)
	flush_data();

#if 0
    if (theBlock->myEntries > 0)
    {
	uint64 last = theBlock->myAddr[theBlock->myEntries-1];
	uint64 lasttype = (last & theTypeMask) >> theTypeShift;
	uint64 lastsize = last >> theSizeShift;
	uint64 totalsize = lastsize + size;
	if (type == lasttype && totalsize <= 128)
	{
	    uint64 lastaddr = last & theAddrMask;
	    // Sequential
	    if (addr == lastaddr + lastsize)
	    {
		last &= ~theSizeMask;
		last |= totalsize << theSizeShift;
		theBlock->myAddr[theBlock->myEntries-1] = last;
		return;
	    }
	    // Sequential, reverse
	    if (lastaddr == addr + size)
	    {
		last = addr;
		last |= type;
		last |= totalsize << theSizeShift;
		theBlock->myAddr[theBlock->myEntries-1] = last;
		return;
	    }
	    // Duplicate
	    if (lastaddr == addr)
	    {
		if (size > lastsize)
		{
		    last &= ~theSizeMask;
		    last |= size << theSizeShift;
		    theBlock->myAddr[theBlock->myEntries-1] = last;
		}
		return;
	    }
	}
    }
#endif

    uint64 data = addr;
    data |= type;
    data |= size << theSizeShift;

    theBlock->myAddr[theBlock->myEntries] = data;
    theBlock->myEntries++;
}

static inline void put_wdata(Addr addr, uint64 type, SizeT size)
{
    uint64  part;
    while (size)
    {
	part = (uint64)(size > 128 ? 128 : size);
	put_data(addr, type, part);
	addr += part;
	size -= part;
    }
}

/*------------------------------------------------------------*/
/*--- Stuff for --trace-mem                                ---*/
/*------------------------------------------------------------*/

#define MAX_DSIZE    512

typedef
   IRExpr 
   IRAtom;

typedef 
   enum { Event_Ir, Event_Dr, Event_Dw, Event_Dm }
   EventKind;

typedef
   struct {
      EventKind  ekind;
      IRAtom*    addr;
      Int        size;
   }
   Event;

/* Up to this many unnotified events are allowed.  Must be at least two,
   so that reads and writes to the same address can be merged into a modify.
   Beyond that, larger numbers just potentially induce more spilling due to
   extending live ranges of address temporaries. */
#define N_EVENTS 16

/* Maintain an ordered list of memory events which are outstanding, in
   the sense that no IR has yet been generated to do the relevant
   helper calls.  The SB is scanned top to bottom and memory events
   are added to the end of the list, merging with the most recent
   notified event where possible (Dw immediately following Dr and
   having the same size and EA can be merged).

   This merging is done so that for architectures which have
   load-op-store instructions (x86, amd64), the instr is treated as if
   it makes just one memory reference (a modify), rather than two (a
   read followed by a write at the same address).

   At various points the list will need to be flushed, that is, IR
   generated from it.  That must happen before any possible exit from
   the block (the end, or an IRStmt_Exit).  Flushing also takes place
   when there is no space to add a new event, and before entering a
   RMW (read-modify-write) section on processors supporting LL/SC.

   If we require the simulation statistics to be up to date with
   respect to possible memory exceptions, then the list would have to
   be flushed before each memory reference.  That's a pain so we don't
   bother.

   Flushing the list consists of walking it start to end and emitting
   instrumentation IR for each event, in the order in which they
   appear. */

static Event events[N_EVENTS];
static Int   events_used = 0;
static Int   canCreateModify = 0;

static VG_REGPARM(2) void trace_instr(Addr addr, SizeT size)
{
    put_data(addr, theShiftedInstr, (uint64)size);
}

static VG_REGPARM(3) void trace_2instr(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, theShiftedInstr, (uint64)size);
    put_data(addr2, theShiftedInstr, (uint64)size);
}

static VG_REGPARM(2) void trace_load(Addr addr, SizeT size)
{
    put_data(addr, theShiftedRead, (uint64)size);
}

static VG_REGPARM(3) void trace_2load(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, theShiftedRead, (uint64)size);
    put_data(addr2, theShiftedRead, (uint64)size);
}

static VG_REGPARM(2) void trace_store(Addr addr, SizeT size)
{
    put_data(addr, theShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_2store(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, theShiftedWrite, (uint64)size);
    put_data(addr2, theShiftedWrite, (uint64)size);
}

static VG_REGPARM(2) void trace_modify(Addr addr, SizeT size)
{
    put_data(addr, theShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_2modify(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, theShiftedWrite, (uint64)size);
    put_data(addr2, theShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_loadstore(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, theShiftedRead, (uint64)size);
    put_data(addr2, theShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_storeload(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, theShiftedWrite, (uint64)size);
    put_data(addr2, theShiftedRead, (uint64)size);
}

static void flushEvents(IRSB* sb)
{
    Int        i;
   const HChar* helperName;
    void*      helperAddr;
    IRExpr**   argv;
    IRDirty*   di;
    Event*     ev;
    Event*     ev2;
    Int        regparms;

    for (i = 0; i < events_used; i++) {

	ev = &events[i];
	ev2 = i < events_used-1 ? &events[i+1] : NULL;

	if (ev2 &&
		ev->ekind == ev2->ekind &&
		ev->size == ev2->size)
	{
	    // Decide on helper fn to call and args to pass it.
	    switch (ev->ekind) {
		case Event_Ir: helperName = "trace_2instr";
			       helperAddr =  trace_2instr;  break;

		case Event_Dr: helperName = "trace_2load";
			       helperAddr =  trace_2load;   break;

		case Event_Dw: helperName = "trace_2store";
			       helperAddr =  trace_2store;  break;

		case Event_Dm: helperName = "trace_2modify";
			       helperAddr =  trace_2modify; break;
		default:
			       tl_assert(0);
	    }

	    argv = mkIRExprVec_3( ev->addr,
				  ev2->addr, mkIRExpr_HWord( ev->size ));
	    regparms = 3;

	    // Skip the next event, since we paired it
	    i++;
	}
	else if (ev2 &&
		ev->ekind == Event_Dr &&
		ev2->ekind == Event_Dw &&
		ev->size == ev2->size)
	{
	    // Load then store
	    helperName = "trace_loadstore";
	    helperAddr = trace_loadstore;

	    argv = mkIRExprVec_3( ev->addr,
				  ev2->addr, mkIRExpr_HWord( ev->size ));
	    regparms = 3;
	    i++;
	}
	else if (ev2 &&
		ev->ekind == Event_Dw &&
		ev2->ekind == Event_Dr &&
		ev->size == ev2->size)
	{
	    // Store then load
	    helperName = "trace_storeload";
	    helperAddr = trace_storeload;

	    argv = mkIRExprVec_3( ev->addr,
				  ev2->addr, mkIRExpr_HWord( ev->size ));
	    regparms = 3;
	    i++;
	}
	else
	{

	    // Decide on helper fn to call and args to pass it.
	    switch (ev->ekind) {
		case Event_Ir: helperName = "trace_instr";
			       helperAddr =  trace_instr;  break;

		case Event_Dr: helperName = "trace_load";
			       helperAddr =  trace_load;   break;

		case Event_Dw: helperName = "trace_store";
			       helperAddr =  trace_store;  break;

		case Event_Dm: helperName = "trace_modify";
			       helperAddr =  trace_modify; break;
		default:
			       tl_assert(0);
	    }

	    argv = mkIRExprVec_2( ev->addr, mkIRExpr_HWord( ev->size ) );
	    regparms = 2;
	}

	// Add the helper.
	di   = unsafeIRDirty_0_N(regparms,
		helperName, VG_(fnptr_to_fnentry)( helperAddr ),
		argv );

	addStmtToIRSB( sb, IRStmt_Dirty(di) );
    }

    events_used = 0;
}

// WARNING:  If you aren't interested in instruction reads, you can omit the
// code that adds calls to trace_instr() in flushEvents().  However, you
// must still call this function, addEvent_Ir() -- it is necessary to add
// the Ir events to the events list so that merging of paired load/store
// events into modify events works correctly.
static void addEvent_Ir ( IRSB* sb, IRAtom* iaddr, UInt isize )
{
    Event* evt;
    tl_assert( (VG_MIN_INSTR_SZB <= isize && isize <= VG_MAX_INSTR_SZB)
	    || VG_CLREQ_SZB == isize );
    if (events_used == N_EVENTS)
	flushEvents(sb);
    tl_assert(events_used >= 0 && events_used < N_EVENTS);
    evt = &events[events_used];
    evt->ekind = Event_Ir;
    evt->addr  = iaddr;
    evt->size  = isize;
    events_used++;
}

static
void addEvent_Dr ( IRSB* sb, IRAtom* daddr, Int dsize )
{
    Event* evt;
    tl_assert(isIRAtom(daddr));
    tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);
    if (events_used == N_EVENTS)
	flushEvents(sb);
    tl_assert(events_used >= 0 && events_used < N_EVENTS);
    evt = &events[events_used];
    evt->ekind = Event_Dr;
    evt->addr  = daddr;
    evt->size  = dsize;
    events_used++;
    canCreateModify = True;
}

static
void addEvent_Dw ( IRSB* sb, IRAtom* daddr, Int dsize )
{
    Event* lastEvt;
    Event* evt;
    tl_assert(isIRAtom(daddr));
    tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);

    // Is it possible to merge this write with the preceding read?
    lastEvt = &events[events_used-1];
    if (canCreateModify && events_used > 0
	    && lastEvt->ekind == Event_Dr
	    && lastEvt->size  == dsize
	    && eqIRAtom(lastEvt->addr, daddr))
    {
	lastEvt->ekind = Event_Dm;
	return;
    }

    // No.  Add as normal.
    if (events_used == N_EVENTS)
	flushEvents(sb);
    tl_assert(events_used >= 0 && events_used < N_EVENTS);
    evt = &events[events_used];
    evt->ekind = Event_Dw;
    evt->size  = dsize;
    evt->addr  = daddr;
    events_used++;
}

//------------------------------------------------------------//
//--- malloc() et al replacement wrappers                  ---//
//------------------------------------------------------------//

// Nb: first two fields must match core's VgHashNode.
typedef struct _HP_Chunk {
    struct _HP_Chunk	*next;
    Addr		 data;
    SizeT		 size;
    SizeT		 fullsize;
} HP_Chunk;

static VgHashTable malloc_list  = NULL;   // HP_Chunks

static __inline__
void* alloc_block ( ThreadId tid, SizeT req_szB, SizeT req_alignB,
	Bool is_zeroed )
{
    HP_Chunk	*hc;
    void	*p;

    if ((SSizeT)req_szB < 0) return NULL;
    //VG_(printf)("salloc: %d\n", req_szB);

    // Allocate and zero if necessary.
    p = VG_(cli_malloc)( req_alignB, req_szB );
    if (!p) {
	return NULL;
    }
    if (is_zeroed) VG_(memset)(p, 0, req_szB);

    // Add to the hash table
    hc = (HP_Chunk *)VG_(malloc)("mv_malloc", sizeof(HP_Chunk));
    hc->data = (Addr)p;
    hc->size = req_szB;
    hc->fullsize = VG_(malloc_usable_size)(p);

    VG_(HT_add_node)(malloc_list, hc);

    put_wdata((Addr)p, theShiftedAlloc, req_szB);

    //VG_(printf)("ealloc: %d\n", req_szB);
    return p;
}

static __inline__
void release_block ( void* p )
{
    //VG_(printf)("sfree: %p\n", p);
    HP_Chunk* hc = VG_(HT_remove)(malloc_list, (UWord)p);

    put_wdata((Addr)p, theShiftedFree, hc->size);

    VG_(free)(hc);
    VG_(cli_free)(p);
    //VG_(printf)("efree: %p\n", p);
}

static __inline__
void* realloc_block ( ThreadId tid, void* p_old, SizeT new_req_szB )
{
    void        *p_new;
    HP_Chunk	*hc = VG_(HT_lookup)(malloc_list, (UWord)p_old);

    if (!hc)
	return NULL;
   
    if (new_req_szB > hc->fullsize)
    {
	p_new = alloc_block(tid, new_req_szB, VG_(clo_alignment), False);
	if (!p_new)
	    return NULL;
	VG_(memcpy)(p_new, p_old, hc->size);
	release_block(p_old);
    }
    else
    {
	p_new = p_old;
	hc->size = new_req_szB;
    }

    return p_new;
}

static void* mv_malloc ( ThreadId tid, SizeT szB )
{
    return alloc_block( tid, szB, VG_(clo_alignment), /*is_zeroed*/False );
}

static void* mv___builtin_new ( ThreadId tid, SizeT szB )
{
    return alloc_block( tid, szB, VG_(clo_alignment), /*is_zeroed*/False );
}

static void* mv___builtin_vec_new ( ThreadId tid, SizeT szB )
{
    return alloc_block( tid, szB, VG_(clo_alignment), /*is_zeroed*/False );
}

static void* mv_calloc ( ThreadId tid, SizeT m, SizeT szB )
{
    return alloc_block( tid, m*szB, VG_(clo_alignment), /*is_zeroed*/True );
}

static void *mv_memalign ( ThreadId tid, SizeT alignB, SizeT szB )
{
    return alloc_block( tid, szB, alignB, False );
}

static void mv_free ( ThreadId tid __attribute__((unused)), void* p )
{
    release_block(p);
}

static void mv___builtin_delete ( ThreadId tid, void* p )
{
    release_block(p);
}

static void mv___builtin_vec_delete ( ThreadId tid, void* p )
{
    release_block(p);
}

static void* mv_realloc ( ThreadId tid, void* p_old, SizeT new_szB )
{
    return realloc_block(tid, p_old, new_szB);
}

static SizeT mv_malloc_usable_size ( ThreadId tid, void* p )
{                                                            
    HP_Chunk* hc = VG_(HT_lookup)( malloc_list, (UWord)p );
    return ( hc ? hc->fullsize : 0 );
}                                                            

/*------------------------------------------------------------*/
/*--- Basic tool functions                                 ---*/
/*------------------------------------------------------------*/

static void mv_post_clo_init(void)
{
    if (clo_shared_mem)
    {
	SysRes	o = VG_(open)(clo_shared_mem, VKI_O_RDWR, 0666);
	if (sr_isError(o))
	{
	    VG_(umsg)("cannot open shared memory file \"%s\"\n", clo_shared_mem);
	    VG_(exit)(1);
	}

	SysRes	res = VG_(am_shared_mmap_file_float_valgrind)
	    (sizeof(SharedData), VKI_PROT_READ|VKI_PROT_WRITE,
	     sr_Res(o), (Off64T)0);
	if (sr_isError(res))
	{
	    VG_(umsg)("mmap failed\n");
	    VG_(exit)(1);
	}

	theSharedData = (SharedData *)(Addr)sr_Res(res);
	VG_(dmsg)("got memory %p\n", theSharedData);

	theBlock = &theSharedData->myBlocks[0];
    }
    else
    {
	theBlock = &theBlockData;
    }
}

static IRSB*
mv_instrument ( VgCallbackClosure* closure,
	IRSB* sbIn, 
	VexGuestLayout* layout, 
	VexGuestExtents* vge,
        VexArchInfo* archinfo_host,
	IRType gWordTy, IRType hWordTy )
{
    Int        i;
    IRSB*      sbOut;
    IRTypeEnv* tyenv = sbIn->tyenv;

    if (gWordTy != hWordTy) {
	/* We don't currently support this case. */
	VG_(tool_panic)("host/guest word size mismatch");
    }

    /* Set up SB */
    sbOut = deepCopyIRSBExceptStmts(sbIn);

    // Copy verbatim any IR preamble preceding the first IMark
    i = 0;
    while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
	addStmtToIRSB( sbOut, sbIn->stmts[i] );
	i++;
    }

    events_used = 0;

    for (/*use current i*/; i < sbIn->stmts_used; i++) {
	IRStmt* st = sbIn->stmts[i];
	if (!st || st->tag == Ist_NoOp) continue;

	switch (st->tag) {
	    case Ist_NoOp:
	    case Ist_AbiHint:
	    case Ist_Put:
	    case Ist_PutI:
	    case Ist_MBE:
		addStmtToIRSB( sbOut, st );
		break;

	    case Ist_IMark:
		canCreateModify = False;
		if (clo_trace_instrs)
		{
		    addEvent_Ir( sbOut,
			    mkIRExpr_HWord( (HWord)st->Ist.IMark.addr ),
			    st->Ist.IMark.len );
		}
		addStmtToIRSB( sbOut, st );
		break;

	    case Ist_WrTmp:
		{
		    // Add a call to trace_load() if --trace-mem=yes.
		    IRExpr* data = st->Ist.WrTmp.data;
		    if (data->tag == Iex_Load) {
			addEvent_Dr( sbOut, data->Iex.Load.addr,
				sizeofIRType(data->Iex.Load.ty) );
		    }
		}
		addStmtToIRSB( sbOut, st );
		break;

	    case Ist_Store:
		{
		    IRExpr* data  = st->Ist.Store.data;
		    addEvent_Dw( sbOut, st->Ist.Store.addr,
			    sizeofIRType(typeOfIRExpr(tyenv, data)) );
		}
		addStmtToIRSB( sbOut, st );
		break;

	    case Ist_Dirty:
		{
		    Int      dsize;
		    IRDirty* d = st->Ist.Dirty.details;
		    if (d->mFx != Ifx_None) {
			// This dirty helper accesses memory.  Collect the details.
			tl_assert(d->mAddr != NULL);
			tl_assert(d->mSize != 0);
			dsize = d->mSize;
			if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
			    addEvent_Dr( sbOut, d->mAddr, dsize );
			if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
			    addEvent_Dw( sbOut, d->mAddr, dsize );
		    } else {
			tl_assert(d->mAddr == NULL);
			tl_assert(d->mSize == 0);
		    }
		    addStmtToIRSB( sbOut, st );
		    break;
		}

	    case Ist_CAS:
		{
		    /* We treat it as a read and a write of the location.  I
		       think that is the same behaviour as it was before IRCAS
		       was introduced, since prior to that point, the Vex
		       front ends would translate a lock-prefixed instruction
		       into a (normal) read followed by a (normal) write. */
		    Int    dataSize;
		    IRType dataTy;
		    IRCAS* cas = st->Ist.CAS.details;
		    tl_assert(cas->addr != NULL);
		    tl_assert(cas->dataLo != NULL);
		    dataTy   = typeOfIRExpr(tyenv, cas->dataLo);
		    dataSize = sizeofIRType(dataTy);
		    if (cas->dataHi != NULL)
			dataSize *= 2; /* since it's a doubleword-CAS */
		    addEvent_Dr( sbOut, cas->addr, dataSize );
		    addEvent_Dw( sbOut, cas->addr, dataSize );
		    addStmtToIRSB( sbOut, st );
		    break;
		}

	    case Ist_LLSC:
		{
		    IRType dataTy;
		    if (st->Ist.LLSC.storedata == NULL) {
			/* LL */
			dataTy = typeOfIRTemp(tyenv, st->Ist.LLSC.result);
			addEvent_Dr( sbOut, st->Ist.LLSC.addr,
				sizeofIRType(dataTy) );
		    } else {
			/* SC */
			dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
			addEvent_Dw( sbOut, st->Ist.LLSC.addr,
				sizeofIRType(dataTy) );
		    }
		    addStmtToIRSB( sbOut, st );
		    break;
		}

	    case Ist_Exit:
		flushEvents(sbOut);

		addStmtToIRSB( sbOut, st );      // Original statement
		break;

	    default:
		tl_assert(0);
	}
    }

    /* At the end of the sbIn.  Flush outstandings. */
    flushEvents(sbOut);

    return sbOut;
}

static void mv_fini(Int exitcode)
{
    flush_data();

    VG_(printf)("Total events: %lld\n", theTotalEvents);
}

static void mv_atfork_child(ThreadId tid)
{
    /* Can't have 2 processes writing to the same pipe. */
    clo_pipe = 0;
}

static void mv_pre_clo_init(void)
{
    VG_(details_name)            ("Memview");
    VG_(details_version)         (NULL);
    VG_(details_description)     ("a memory trace generator");
    VG_(details_copyright_author)(
	    "Copyright (C) 2011-2012, and GNU GPL'd, by Andrew Clinton.");
    VG_(details_bug_reports_to)  (VG_BUGS_TO);
    VG_(details_avg_translation_sizeB) ( 200 );

    VG_(basic_tool_funcs)          (mv_post_clo_init,
	    mv_instrument,
	    mv_fini);
    VG_(needs_command_line_options)(mv_process_cmd_line_option,
	    mv_print_usage,
	    mv_print_debug_usage);
    VG_(needs_malloc_replacement)  (mv_malloc,
	    mv___builtin_new,
	    mv___builtin_vec_new,
	    mv_memalign,
	    mv_calloc,
	    mv_free,
	    mv___builtin_delete,
	    mv___builtin_vec_delete,
	    mv_realloc,
	    mv_malloc_usable_size,
	    0 );

    malloc_list = VG_(HT_construct)("Memview's malloc list");

    VG_(atfork)(NULL/*pre*/, NULL/*parent*/, mv_atfork_child/*child*/);
}

VG_DETERMINE_INTERFACE_VERSION(mv_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                mv_main.c ---*/
/*--------------------------------------------------------------------*/
