
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
#include "coregrind/pub_core_aspacemgr.h"

/* This define enables the malloc() wrapping callbacks in the tool runtime.
   To disable wrapping entirely you also need to remove the
   vgpreload_memview-*.so file. */
#define MV_WRAP_MALLOC

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
	// Malloc wrapping supports --trace-malloc but not other malloc
	// replacement options.
	return VG_(replacement_malloc_process_cmd_line_option)(arg);

    return True;
}

static void mv_print_usage(void)
{  
    VG_(printf)(
	    "    --pipe=<fd>                pipe to fd [0]\n"
//	    "    --shared-mem=<file>        shared memory output file [""]\n"
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

// Data for pipe
static MV_TraceBlock	 theBlockData;
// Data for shm
static MV_SharedData	*theSharedData = 0;

typedef unsigned long long uint64;
static uint64		 theTotalEvents = 0;

static MV_StackInfo	 theStackTrace;

static void appendIpDesc(UInt n, Addr ip, void* uu_opaque)
{
    MV_StackInfo	*sbuf = (MV_StackInfo *)uu_opaque;
    HChar		 tmp[MV_STACKTRACE_BUFSIZE];

    VG_(describe_IP)(ip, tmp, MV_STACKTRACE_BUFSIZE);

    int available = MV_STACKTRACE_BUFSIZE - sbuf->mySize;
    int len =
	VG_(snprintf)(
		&sbuf->myBuf[sbuf->mySize],
		available,
		"%s%s %s",
		( sbuf->mySize ? "\n" : ""),
		( n == 0 ? "at" : "by" ), tmp);

    if (len >= available)
	sbuf->mySize += available-1;
    else
	sbuf->mySize += len;
}

static void flush_data(void)
{
    if (clo_shared_mem)
    {
	// Not implemented
    }
    else if (clo_pipe)
    {
	MV_Header	header;

	//
	// Stack traces are retained until the next block is flushed.  This
	// is to allow the stack trace to correspond with the first address
	// in the subsequent data block.
	//

	// Send the pending stack trace
	if (theStackTrace.mySize)
	{
	    header.myType = MV_STACKTRACE;
	    header.mySize = theStackTrace.mySize+1; // Include terminating '\0'
	    header.mySize += sizeof(uint64);

	    theStackTrace.myAddr = theBlockData.myAddr[0];

	    VG_(write)(clo_pipe, &header, sizeof(MV_Header));
	    VG_(write)(clo_pipe, &theStackTrace, header.mySize);
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

	theStackTrace.mySize = 0;
	VG_(apply_StackTrace)(appendIpDesc, &theStackTrace, ips, n_ips);

	// Send the block
	header.myType = MV_BLOCK;

	VG_(write)(clo_pipe, &header, sizeof(MV_Header));
	VG_(write)(clo_pipe, &theBlockData, sizeof(theBlockData));

    }

#if 0
    VG_(printf)("flush_data: %d\n", theBlock->myEntries);
    int i;
    for (i = 0; i < theBlock->myEntries; i++)
    {
	VG_(printf)("addr: %llx\n", theBlock->myAddr[i]);
    }
#endif

    theTotalEvents += theBlock->myEntries;
    theBlock->myEntries = 0;
}

static inline void put_data(Addr addr, uint64 type, uint64 size)
{
    if (theBlock->myEntries >= MV_BlockSize)
	flush_data();

    // This code was written in the hope that it might improve performance
    // by reducing the amount of data sent on the pipe.  Unfortunately, it
    // actually decreased performance.
#if 0
    if (theBlock->myEntries > 0)
    {
	uint64 last = theBlock->myAddr[theBlock->myEntries-1];
	uint64 lasttype = (last & MV_TypeMask) >> MV_TypeShift;
	uint64 lastsize = last >> MV_SizeShift;
	uint64 totalsize = lastsize + size;
	if (type == lasttype && totalsize <= 128)
	{
	    uint64 lastaddr = last & MV_AddrMask;
	    // Sequential
	    if (addr == lastaddr + lastsize)
	    {
		last &= ~MV_SizeMask;
		last |= totalsize << MV_SizeShift;
		theBlock->myAddr[theBlock->myEntries-1] = last;
		return;
	    }
	    // Sequential, reverse
	    if (lastaddr == addr + size)
	    {
		last = addr;
		last |= type;
		last |= totalsize << MV_SizeShift;
		theBlock->myAddr[theBlock->myEntries-1] = last;
		return;
	    }
	    // Duplicate
	    if (lastaddr == addr)
	    {
		if (size > lastsize)
		{
		    last &= ~MV_SizeMask;
		    last |= size << MV_SizeShift;
		    theBlock->myAddr[theBlock->myEntries-1] = last;
		}
		return;
	    }
	}
    }
#endif

    // This same encoding is created with VEX IR in flushEventsIR().
    uint64 data = addr;
    data |= type;
    data |= size << MV_SizeShift;

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
    put_data(addr, MV_ShiftedInstr, (uint64)size);
}

static VG_REGPARM(3) void trace_2instr(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedInstr, (uint64)size);
    put_data(addr2, MV_ShiftedInstr, (uint64)size);
}

static VG_REGPARM(2) void trace_load(Addr addr, SizeT size)
{
    put_data(addr, MV_ShiftedRead, (uint64)size);
}

static VG_REGPARM(3) void trace_2load(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedRead, (uint64)size);
    put_data(addr2, MV_ShiftedRead, (uint64)size);
}

static VG_REGPARM(2) void trace_store(Addr addr, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_2store(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint64)size);
    put_data(addr2, MV_ShiftedWrite, (uint64)size);
}

static VG_REGPARM(2) void trace_modify(Addr addr, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_2modify(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint64)size);
    put_data(addr2, MV_ShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_loadstore(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedRead, (uint64)size);
    put_data(addr2, MV_ShiftedWrite, (uint64)size);
}

static VG_REGPARM(3) void trace_storeload(Addr addr, Addr addr2, SizeT size)
{
    put_data(addr, MV_ShiftedWrite, (uint64)size);
    put_data(addr2, MV_ShiftedRead, (uint64)size);
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

/* This version of flushEvents avoids callbacks entirely, except when the
   number of outstanding events is enough to be flushed - in which case a
   call to flush_data() is made.  In all other cases, events are handled by
   creating IR to encode and store the memory access information to the
   array of outstanding events.  */
static void flushEventsIR(IRSB* sb)
{
    if (!events_used)
	return;

    /* What's the native endianness?  We need to know this. */
    IREndness end;
#  if defined(VG_BIGENDIAN)
    end = Iend_BE;
#  elif defined(VG_LITTLEENDIAN)
    end = Iend_LE;
#  else
#    error "Unknown endianness"
#  endif

    // Conditionally call the flush method if there's not enough room for
    // all the new events.  This may flush an incomplete block.
    IRExpr *entries_addr = mkU64((ULong)&theBlock->myEntries);
    IRExpr *entries = load(end, Ity_I32, entries_addr);

    IRDirty*   di =
	unsafeIRDirty_0_N(0,
	    "flush_data", VG_(fnptr_to_fnentry)( flush_data ),
	    mkIRExprVec_0() );

    di->guard =
	binop(Iop_CmpLE32S, mkU32(MV_BlockSize - events_used), entries);

    addStmtToIRSB( sb, IRStmt_Dirty(di) );

    // Reload entries since it might have been changed by the callback
    entries = load(end, Ity_I32, entries_addr);

    // Initialize the first address where we'll write trace information.
    // This will be advanced in the loop.
    uint64 addr_size = sizeof(uint64);
    IRExpr *addr =
	binop(Iop_Add64,
		mkU64((ULong)theBlock->myAddr),
		unop(Iop_32Uto64,
		    binop(Iop_Mul32, entries, mkU32(addr_size))));

    Int        i;
    for (i = 0; i < events_used; i++) {

	Event*     ev = &events[i];

	uint64 type = 0;
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

	// Construct the address and store it
	IRExpr *data =
	    binop(Iop_Or64, ev->addr,
		    mkU64(type | ((uint64)ev->size << MV_SizeShift)));

	IRStmt *store = IRStmt_Store(end, addr, data);

	addStmtToIRSB( sb, store );

	// Advance to the next entry
	addr = binop(Iop_Add64, addr, mkU64(addr_size));
    }

    // Store the new entry count
    IRStmt *entries_store =
	IRStmt_Store(end, entries_addr,
		binop(Iop_Add32, entries, mkU32(events_used)));

    addStmtToIRSB( sb, entries_store );

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
	VG_(dmsg)("got memory %p\n", theSharedData);

	VG_(umsg)("shared memory interface not implemented\n");
	VG_(exit)(1);
    }
    else
    {
	theBlock = &theBlockData;
    }
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
    /* Can't have 2 processes writing to the same pipe, so arbitrarily
       choose to continue tracing only the parent. Perhaps this should use
       the --trace-children option to decide whether the parent or child
       should continue writing to the pipe? */
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
