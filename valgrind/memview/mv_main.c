
/*--------------------------------------------------------------------*/
/*--- Memview: A tool to export real-time memory trace information ---*/
/*--- via IPC.                                                     ---*/
/*---                                                    mv_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Memview.

   Copyright (C) 2000-2012 Julian Seward 
      jseward@acm.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

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
#include "pub_tool_threadstate.h"
#include "pub_tool_aspacehl.h"
#include "pub_tool_aspacemgr.h"
#include "coregrind/pub_core_aspacemgr.h"
/* This define enables the malloc() wrapping callbacks in the tool runtime.
   To disable wrapping entirely you also need to remove the
   vgpreload_memview-*.so file. */
#define MV_WRAP_MALLOC

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

static int		 clo_pipe = 0;
static int		 clo_inpipe = 0;
static Bool		 clo_trace_instrs = False;
static const char	*clo_shared_mem = 0;

static Bool mv_process_cmd_line_option(const HChar* arg)
{
    if VG_INT_CLO(arg, "--pipe",		clo_pipe) {}
    else if VG_INT_CLO(arg, "--inpipe",		clo_inpipe) {}
    else if VG_STR_CLO(arg, "--shared-mem",	clo_shared_mem) {}
    else if VG_BOOL_CLO(arg, "--trace-instrs",	clo_trace_instrs) {}
    else
	// Malloc wrapping supports --trace-malloc but not other malloc
	// replacement options.
	return VG_(replacement_malloc_process_cmd_line_option)(arg);

    return True;
}

static void mv_print_usage(void)
{  
    VG_(printf)(
	    "    --pipe=<fd>                pipe to fd [0]\n"
	    "    --inpipe=<fd>              input pipe from fd [0]\n"
	    "    --shared-mem=<file>        shared memory output file [""]\n"
	    "    --trace-instrs=yes         trace instruction memory [no]\n"
	    );
}

static void mv_print_debug_usage(void)
{  
    VG_(printf)("    (none)\n");
}

/*------------------------------------------------------------*/
/*--- Memory Trace IPC					      */
/*------------------------------------------------------------*/

static MV_TraceBlock	*theBlock = 0;
static unsigned int	 theEntries = 0;
static unsigned int	 theMaxEntries = 1;

// Data for pipe
static MV_TraceBlock	 theBlockData;
// Data for shm
static MV_SharedData	*theSharedData = 0;
static int		 theBlockIndex = 0;

typedef unsigned long long  uint64;
typedef unsigned int	    uint32;

static uint64		 theTotalEvents = 0;

static MV_StackInfo	 theStackInfo;
static char		 theStackTrace[MV_STR_BUFSIZE];

static uint32		 theThread = 0;

static void appendIpDesc(UInt n, Addr ip, void* uu_opaque)
{
    HChar		 tmp[MV_STR_BUFSIZE];

    VG_(describe_IP)(ip, tmp, MV_STR_BUFSIZE);

    int available = MV_STR_BUFSIZE - theStackInfo.mySize;
    int len =
	VG_(snprintf)(
		&theStackTrace[theStackInfo.mySize],
		available,
		"%s%s %s",
		( theStackInfo.mySize ? "\n" : ""),
		( n == 0 ? "at" : "by" ), tmp);

    if (len >= available)
	theStackInfo.mySize += available-1;
    else
	theStackInfo.mySize += len;
}

static void flush_data(void)
{
    theTotalEvents += theEntries;

    if (clo_pipe)
    {
	MV_Header	header;

	//
	// Stack traces are retained until the next block is flushed.  This
	// is to allow the stack trace to correspond with the first address
	// in the subsequent data block.
	//

	// Send the pending stack trace
	if (theStackInfo.mySize)
	{
	    header.myType = MV_STACKTRACE;
	    header.myStack = theStackInfo;
	    header.myStack.mySize += 1; // Include terminating '\0'
	    header.myStack.myAddr = theBlock->myAddr[0];

	    VG_(write)(clo_pipe, &header, sizeof(MV_Header));
	    VG_(write)(clo_pipe, theStackTrace, header.myStack.mySize);
	}

	// Prepare the next stack trace
	Int ncallers = VG_(clo_backtrace_size);
	Addr ips[ncallers];
	UInt n_ips = VG_(get_StackTrace)(
		VG_(get_running_tid)(),
		ips, ncallers,
		NULL/*array to dump SP values in*/,
		NULL/*array to dump FP values in*/,
		0/*first_ip_delta*/);

	theStackInfo.mySize = 0;
	VG_(apply_StackTrace)(appendIpDesc, 0, ips, n_ips);

	// Send the block
	header.myType = MV_BLOCK;
	theBlock->myEntries = theEntries;

	VG_(write)(clo_pipe, &header, sizeof(MV_Header));

	// Wait for max entries token
	VG_(read)(clo_inpipe, &theMaxEntries, sizeof(int));

	theBlockIndex++;
	if (theBlockIndex == MV_BufCount)
	    theBlockIndex = 0;

	theBlock = &theSharedData->myData[theBlockIndex];
    }

#if 0
    VG_(printf)("flush_data: %d\n", theEntries);
    int i;
    for (i = 0; i < theEntries; i++)
    {
	VG_(printf)("addr: %llx\n", theBlock->myAddr[i].myAddr);
    }
#endif

    theEntries = 0;
}

static inline void put_data(Addr addr, uint32 type, uint32 size)
{
    if (theEntries >= theMaxEntries)
	flush_data();

    // This same encoding is created with VEX IR in flushEventsIR().
    type |= theThread;
    type |= size << MV_SizeShift;

    theBlock->myAddr[theEntries].myAddr = addr;
    theBlock->myAddr[theEntries].myType = type;
    theEntries++;
}

static inline void put_wdata(Addr addr, uint32 type, SizeT size)
{
    uint32  part;
    while (size)
    {
	part = (uint32)(size > 128 ? 128 : size);
	put_data(addr, type, part);
	addr += part;
	size -= part;
    }
}

/*------------------------------------------------------------*/
/*--- instrumentation (based on lackey)                    ---*/
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
      Int	 type;
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
    put_data(addr, MV_ShiftedInstr, (uint32)size);
}

static VG_REGPARM(3) void trace_2instr(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedInstr, (uint32)size);
    put_data(addr2, MV_ShiftedInstr, (uint32)size);
}

static VG_REGPARM(2) void trace_load(Addr addr, SizeT size)
{
    put_data(addr, MV_ShiftedRead, (uint32)size);
}

static VG_REGPARM(3) void trace_2load(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedRead, (uint32)size);
    put_data(addr2, MV_ShiftedRead, (uint32)size);
}

static VG_REGPARM(2) void trace_store(Addr addr, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint32)size);
}

static VG_REGPARM(3) void trace_2store(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint32)size);
    put_data(addr2, MV_ShiftedWrite, (uint32)size);
}

static VG_REGPARM(2) void trace_modify(Addr addr, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint32)size);
}

static VG_REGPARM(3) void trace_2modify(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint32)size);
    put_data(addr2, MV_ShiftedWrite, (uint32)size);
}

static VG_REGPARM(3) void trace_loadstore(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedRead, (uint32)size);
    put_data(addr2, MV_ShiftedWrite, (uint32)size);
}

static VG_REGPARM(3) void trace_storeload(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint32)size);
    put_data(addr2, MV_ShiftedRead, (uint32)size);
}

/* This version of flushEvents (currently unused) is similar to the one in
   lackey with the primary difference that it groups together pairs of
   events for a single callback.  This helps to reduce the total amount of
   function call overhead. */
static void flushEventsCB(IRSB* sb)
{
    IRDirty*   di;
    Int        i;
    for (i = 0; i < events_used; i++) {

	Event*     ev = &events[i];

	const HChar* helperName;
	void*      helperAddr;
	IRExpr**   argv;
	Event*     ev2;
	Int        regparms;

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

/* Code copied from memcheck and modified to aid the creation of flat IR
   from a function tree. Why can't we create tree IR during
   instrumentation? */

#define triop(_op, _arg1, _arg2, _arg3) \
                                 assignNew(sb, IRExpr_Triop((_op),(_arg1),(_arg2),(_arg3)))
#define binop(_op, _arg1, _arg2) assignNew(sb, IRExpr_Binop((_op),(_arg1),(_arg2)))
#define unop(_op, _arg)          assignNew(sb, IRExpr_Unop((_op),(_arg)))
#define load(_op, _ty, _arg)     assignNew(sb, IRExpr_Load((_op),(_ty),(_arg)))
#define mkU8(_n)                 IRExpr_Const(IRConst_U8(_n))
#define mkU16(_n)                IRExpr_Const(IRConst_U16(_n))
#define mkU32(_n)                IRExpr_Const(IRConst_U32(_n))
#define mkU64(_n)                IRExpr_Const(IRConst_U64(_n))
#define mkV128(_n)               IRExpr_Const(IRConst_V128(_n))
#define mkexpr(_tmp)             IRExpr_RdTmp((_tmp))

/* assign value to tmp */
static inline 
void assign ( IRSB* sb, IRTemp tmp, IRExpr* expr ) {
   addStmtToIRSB( sb, IRStmt_WrTmp(tmp, expr) );
}

static IRAtom* assignNew ( IRSB* sb, IRExpr* e )
{
   IRTemp   t;
   IRType   ty = typeOfIRExpr(sb->tyenv, e);

   t = newIRTemp(sb->tyenv, ty);
   assign(sb, t, e);
   return mkexpr(t);
}

/* What's the native endianness?  We need to know this. */
#if defined(VG_BIGENDIAN)
#define ENDIAN Iend_BE
#elif defined(VG_LITTLEENDIAN)
#define ENDIAN Iend_LE
#else
#error "Unknown endianness"
#endif


/* This version of flushEvents avoids callbacks entirely, except when the
   number of outstanding events is enough to be flushed - in which case a
   call to flush_data() is made.  In all other cases, events are handled by
   creating IR to encode and store the memory access information to the
   array of outstanding events.  */
static void flushEventsRange(IRSB* sb, Int start, Int size)
{
    // Conditionally call the flush method if there's not enough room for
    // all the new events.  This may flush an incomplete block.
    IRExpr *entries_addr = mkU64((ULong)&theEntries);
    IRExpr *entries = load(ENDIAN, Ity_I32, entries_addr);

    IRExpr *max_entries_addr = mkU64((ULong)&theMaxEntries);
    IRExpr *max_entries = load(ENDIAN, Ity_I32, max_entries_addr);

    IRDirty*   di =
	unsafeIRDirty_0_N(0,
	    "flush_data", VG_(fnptr_to_fnentry)( flush_data ),
	    mkIRExprVec_0() );

    di->guard =
	binop(Iop_CmpLT32S, max_entries,
		binop(Iop_Add32, entries, mkU32(size)));

    addStmtToIRSB( sb, IRStmt_Dirty(di) );

    // Reload entries since it might have been changed by the callback
    entries = load(ENDIAN, Ity_I32, entries_addr);

    // Initialize the first address where we'll write trace information.
    // This will be advanced in the loop.
    IRExpr *addr =
	binop(Iop_Add64,
		load(ENDIAN, Ity_I64, mkU64((ULong)&theBlock)),
		unop(Iop_32Uto64,
		    binop(Iop_Mul32, entries, mkU32(sizeof(MV_TraceAddr)))));

    // Grab the thread id
    IRExpr *thread = load(ENDIAN, Ity_I32, mkU64((ULong)&theThread));

    Int        i;
    for (i = start; i < start+size; i++) {

	Event*     ev = &events[i];

	uint32 type = 0;
	switch (ev->ekind) {
	    case Event_Ir:
		type = MV_ShiftedInstr;
		break;
	    case Event_Dr:
		type = MV_ShiftedRead;
		break;
	    case Event_Dw:
	    case Event_Dm:
		type = MV_ShiftedWrite;
		break;
	    default:
		tl_assert(0);
	}

	type |= ev->type << MV_DataShift;
	type |= ((uint32)ev->size << MV_SizeShift);

	// Construct the address and store it
	IRExpr *data = binop(Iop_Or32, mkU32(type), thread);

	IRStmt *store;

	store = IRStmt_Store(ENDIAN, addr, ev->addr);
	addStmtToIRSB( sb, store );

	// Advance to the type
	addr = binop(Iop_Add64, addr, mkU64(sizeof(uint64)));

	store = IRStmt_Store(ENDIAN, addr, data);
	addStmtToIRSB( sb, store );

	// Advance to the next entry
	addr = binop(Iop_Add64, addr, mkU64(sizeof(MV_TraceAddr)-sizeof(uint64)));
    }

    // Store the new entry count
    IRStmt *entries_store =
	IRStmt_Store(ENDIAN, entries_addr,
		binop(Iop_Add32, entries, mkU32(size)));

    addStmtToIRSB( sb, entries_store );
}

static void flushEventsIR(IRSB *sb)
{
    Int i;
    for (i = 0; i < events_used; i += theMaxEntries)
    {
	Int size = events_used - i;
	if (size > theMaxEntries)
	    size = theMaxEntries;

	flushEventsRange(sb, i, size);
    }
    events_used = 0;
}

static void flushEvents(IRSB* sb)
{
    flushEventsIR(sb);
}

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
    evt->type  = MV_DataInt32;
    events_used++;
}

static
void addEvent_Dr ( IRSB* sb, IRAtom* daddr, Int dsize, Int type )
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
    evt->type  = type;
    events_used++;
    canCreateModify = True;
}

static
void addEvent_Dw ( IRSB* sb, IRAtom* daddr, Int dsize, Int type )
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
	    && lastEvt->type == type 
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
    evt->type  = type;
    events_used++;
}

//------------------------------------------------------------//
//--- malloc() et al wrapper wrappers                      ---//
//------------------------------------------------------------//

#if defined(MV_WRAP_MALLOC)

// Nb: first two fields must match core's VgHashNode.
typedef struct _HP_Chunk {
    struct _HP_Chunk	*next;
    Addr		 data;
    SizeT		 size;
} HP_Chunk;

static VgHashTable malloc_list  = NULL;   // HP_Chunks


static void mv_malloc ( ThreadId tid, void* p, SizeT szB )
{
    if (!p)
	return;

    HP_Chunk	*hc = VG_(HT_lookup)(malloc_list, (UWord)p);

    // Add to the hash table
    if (!hc)
    {
	hc = (HP_Chunk *)VG_(malloc)("mv_malloc", sizeof(HP_Chunk));
	hc->data = (Addr)p;
	hc->size = szB;

	VG_(HT_add_node)(malloc_list, hc);

	put_wdata((Addr)p, MV_ShiftedAlloc, szB);
    }
}

static void mv_free ( ThreadId tid __attribute__((unused)), void* p )
{
    HP_Chunk* hc = VG_(HT_remove)(malloc_list, (UWord)p);

    if (hc)
    {
	put_wdata((Addr)p, MV_ShiftedFree, hc->size);

	VG_(free)(hc);
    }
}

static void mv_realloc ( ThreadId tid, void* p_new, void* p_old, SizeT new_szB )
{
    if (p_new != p_old)
    {
	mv_free(tid, p_old);
	mv_malloc(tid, p_new, new_szB);
    }
    else
    {
	HP_Chunk	*hc = VG_(HT_lookup)(malloc_list, (UWord)p_old);

	if (!hc)
	    return;
   
	if (new_szB > hc->size)
	    put_wdata((Addr)p_new + hc->size,
		    MV_ShiftedAlloc, new_szB - hc->size);
	else if (new_szB < hc->size)
	    put_wdata((Addr)p_new + new_szB,
		    MV_ShiftedFree, hc->size - new_szB);

	hc->size = new_szB;
    }
}

#endif

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
	    (sizeof(MV_SharedData), VKI_PROT_READ|VKI_PROT_WRITE,
	     sr_Res(o), (Off64T)0);
	if (sr_isError(res))
	{
	    VG_(umsg)("mmap failed\n");
	    VG_(exit)(1);
	}

	theSharedData = (MV_SharedData *)(Addr)sr_Res(res);
	//VG_(dmsg)("got memory %p\n", theSharedData);

	theBlockIndex = 0;
	theBlock = &theSharedData->myData[theBlockIndex];
    }
    else
    {
	theBlock = &theBlockData;
    }
}

static Int
IRTypeToMVType(IRType type)
{
    switch (type)
    {
	case Ity_INVALID:
	case Ity_I1: return MV_DataInt32;
	case Ity_I8: return MV_DataChar8;
	case Ity_I16:
	case Ity_I32: return MV_DataInt32;
	case Ity_I64:
	case Ity_I128: return MV_DataInt64;
	case Ity_F32: return MV_DataFlt32;
	case Ity_F64: return MV_DataFlt64;
	case Ity_D32: return MV_DataInt32;
	case Ity_D64: return MV_DataInt64;
	case Ity_D128: return MV_DataVec;
	case Ity_F128: return MV_DataVec;
	case Ity_V128: return MV_DataVec;
	case Ity_V256: return MV_DataVec;
    }
    return MV_DataInt32;
}

/* This is copied mostly verbatim from lackey */
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

    //ppIRSB(sbIn);

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
		    IRExpr* data = st->Ist.WrTmp.data;
		    if (data->tag == Iex_Load) {
			addEvent_Dr( sbOut, data->Iex.Load.addr,
				sizeofIRType(data->Iex.Load.ty),
			       IRTypeToMVType(data->Iex.Load.ty) );
		    }
		}
		addStmtToIRSB( sbOut, st );
		break;

	    case Ist_Store:
		{
		    IRExpr* data  = st->Ist.Store.data;
		    addEvent_Dw( sbOut, st->Ist.Store.addr,
			    sizeofIRType(typeOfIRExpr(tyenv, data)),
			   IRTypeToMVType(typeOfIRExpr(tyenv, data)) );
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
			    addEvent_Dr( sbOut, d->mAddr, dsize, MV_DataInt32 );
			if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
			    addEvent_Dw( sbOut, d->mAddr, dsize, MV_DataInt32 );
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
		    addEvent_Dr( sbOut, cas->addr, dataSize,
			    IRTypeToMVType(dataTy) );
		    addEvent_Dw( sbOut, cas->addr, dataSize,
			    IRTypeToMVType(dataTy) );
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
				sizeofIRType(dataTy),
			       IRTypeToMVType(dataTy) );
		    } else {
			/* SC */
			dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
			addEvent_Dw( sbOut, st->Ist.LLSC.addr,
				sizeofIRType(dataTy),
				IRTypeToMVType(dataTy) );
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
    /* Can't have 2 processes writing to the same pipe, so arbitrarily
       choose to continue tracing only the parent. Perhaps this should use
       the --trace-children option to decide whether the parent or child
       should continue writing to the pipe? */
    clo_pipe = 0;
    clo_inpipe = 0;
}

static void
mv_mmap_info(Addr a, SizeT len, MV_MMapType type, int thread, const HChar *filename)
{
    if (!clo_pipe)
	return;

    // Flush outstanding events to ensure consistent ordering.  Avoid
    // calling this with 0 entries.
    if (theEntries)
	flush_data();

    MV_Header	header;

    header.myType = MV_MMAP;
    header.myMMap.myStart = a;
    header.myMMap.myEnd = a + len;
    header.myMMap.myType = type;
    header.myMMap.myThread = thread;

    if (!filename)
	filename = "\0";

    header.myMMap.mySize = VG_(strlen)(filename)+1; // Include terminating '\0'

    VG_(write)(clo_pipe, &header, sizeof(MV_Header));
    VG_(write)(clo_pipe, filename, header.myMMap.mySize);
}

static void mv_new_mem_mmap(Addr a, SizeT len,
	Bool rr, Bool ww, Bool xx,
	ULong di_handle)
{
    const NSegment  *info = VG_(am_find_nsegment)(a);
    HChar	    *filename = 0;
    MV_MMapType	     type = MV_HEAP;

    switch (info->kind)
    {
	case SkAnonC: type = MV_HEAP; break;
	case SkShmC: type = MV_SHM; break;
	case SkFileC:
	    if (info->hasX && !info->hasW)
		type = MV_CODE;
	    else
		type = MV_DATA;
	    filename = VG_(am_get_filename)(info);
	    break;
	case SkFree:
	case SkAnonV:
	case SkFileV:
	case SkResvn:
	    // Uninteresting events
	    return;
    }

    mv_mmap_info(a, len, type, 0, filename);
}

static void mv_copy_mem_remap(Addr from, Addr to, SizeT len)
{
    // TODO
}

static void mv_die_mem_munmap(Addr a, SizeT len)
{
    mv_mmap_info(a, len, MV_UNMAP, 0, 0);
}

static void mv_new_mem_stack_signal(Addr a, SizeT len, ThreadId tid)
{
    mv_mmap_info(a, len, MV_STACK, tid, 0);
}

static void mv_die_mem_stack_signal(Addr a, SizeT len)
{
    mv_mmap_info(a, len, MV_UNMAP, 0, 0);
}

static void mv_new_mem_brk(Addr a, SizeT len, ThreadId tid)
{
    mv_mmap_info(a, len, MV_HEAP, tid, 0);
}

static void mv_die_mem_brk(Addr a, SizeT len)
{
    mv_mmap_info(a, len, MV_UNMAP, 0, 0);
}

static void mv_start_client_code(ThreadId tid, ULong blocks_dispatched)
{
    theThread = (uint32)tid << MV_ThreadShift;
}

static void mv_thread_start(ThreadId tid)
{
    Addr end = VG_(thread_get_stack_max)(tid);
    Addr size = VG_(thread_get_stack_size)(tid);

    mv_mmap_info(end-size, size, MV_STACK, tid, 0);
}

static void mv_thread_exit(ThreadId tid)
{
    Addr end = VG_(thread_get_stack_max)(tid);
    Addr size = VG_(thread_get_stack_size)(tid);

    mv_mmap_info(end-size, size, MV_UNMAP, tid, 0);
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

    VG_(track_new_mem_startup)(mv_new_mem_mmap);
    VG_(track_new_mem_mmap)(mv_new_mem_mmap);
    VG_(track_die_mem_munmap)(mv_die_mem_munmap);
    VG_(track_copy_mem_remap)(mv_copy_mem_remap);
    VG_(track_new_mem_stack_signal)(mv_new_mem_stack_signal);
    VG_(track_die_mem_stack_signal)(mv_die_mem_stack_signal);
    VG_(track_new_mem_brk)(mv_new_mem_brk);
    VG_(track_die_mem_brk)(mv_die_mem_brk);

    VG_(track_start_client_code)(mv_start_client_code);
    VG_(track_pre_thread_first_insn)(mv_thread_start);
    VG_(track_pre_thread_ll_exit)(mv_thread_exit);

#if defined(MV_WRAP_MALLOC)
    VG_(needs_malloc_wrap)(
	    mv_malloc,
	    mv_free,
	    mv_realloc);

    malloc_list = VG_(HT_construct)("Memview's malloc list");
#endif

    VG_(atfork)(NULL/*pre*/, NULL/*parent*/, mv_atfork_child/*child*/);
}

VG_DETERMINE_INTERFACE_VERSION(mv_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                mv_main.c ---*/
/*--------------------------------------------------------------------*/
