/*
   This file is part of memview, a real-time memory trace visualization
   application.

   Copyright (C) 2013 Andrew Clinton

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

#include "pin.H"
#include "../mv_ipc.h"
#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* ================================================================== */
// Global variables 
/* ================================================================== */

BUFFER_ID   theBuffer;

struct BufferData {
    ADDRINT     ea;
    UINT64	type;
};

#define NUM_BUF_PAGES 4

static MV_SharedData	*theSharedData = 0;
static MV_TraceBlock	*theBlock = 0;
static unsigned int	 theBlockIndex = 0;
static unsigned int	 theMaxEntries = 1;

static unsigned long long   theTotalEvents = 0;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<UINT32> KnobPipe(KNOB_MODE_WRITEONCE,  "pintool",
    "pipe", "0", "Specify the output pipe");

KNOB<UINT32>   KnobInPipe(KNOB_MODE_WRITEONCE,  "pintool",
    "inpipe", "0", "Specify the input pipe");

KNOB<string>   KnobSharedMem(KNOB_MODE_WRITEONCE,  "pintool",
    "shared-mem", "/dev/shm/memview", "Shared memory file");

KNOB<BOOL>   KnobTraceInstrs(KNOB_MODE_WRITEONCE,  "pintool",
    "trace-instrs", "0", "Trace instruction memory");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool generates a memory trace for use with memview." << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

static void
flushEvents()
{
    if (!theBlock->myEntries)
	return;

    theTotalEvents += theBlock->myEntries;

    // Send the block
    MV_Header	header;
    header.myType = MV_BLOCK;

    if (!write(KnobPipe, &header, sizeof(MV_Header)))
	;

    // Wait for max entries token
    if (!read(KnobInPipe, &theMaxEntries, sizeof(int)))
	;

    theBlockIndex++;
    if (theBlockIndex == MV_BufCount)
	theBlockIndex = 0;

    theBlock = &theSharedData->myData[theBlockIndex];
    theBlock->myEntries = 0;
}

PIN_LOCK    theLock;

/*!
 * Called when a buffer fills up, or the thread exits, so we can process it or pass it off
 * as we see fit.
 * @param[in] id		buffer handle
 * @param[in] tid		id of owning thread
 * @param[in] ctxt		application context
 * @param[in] buf		actual pointer to buffer
 * @param[in] numElements	number of records
 * @param[in] v			callback value
 * @return  A pointer to the buffer to resume filling.
 */
VOID * BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT *ctxt, VOID *buf,
                  UINT64 n, VOID *v)
{
    struct BufferData *data = (struct BufferData *)buf;

    PIN_GetLock(&theLock, tid);
    for (UINT64 i = 0; i < n; i++)
    {
	unsigned long long val;
	val = ((unsigned long long)data[i].ea << MV_AddrShift) & MV_AddrMask;
	val |= data[i].type;
	val |= ((unsigned long long)tid << MV_ThreadShift) & MV_ThreadMask;
	theBlock->myAddr[theBlock->myEntries] = val;
	theBlock->myEntries++;
	if (theBlock->myEntries >= theMaxEntries)
	    flushEvents();
    }
    PIN_ReleaseLock(&theLock);

    return buf;
}

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v)
{
    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
	bool write = INS_MemoryOperandIsWritten(ins, memOp);
	UINT32 size = INS_MemoryOperandSize(ins, memOp);

	unsigned long long  type;
	unsigned long long  datatype;

	if (size <= 1)
	    datatype = MV_DataChar8;
	else if (size <= 4)
	    datatype = MV_DataInt32;
	else
	    datatype = MV_DataInt64;

	type = write ? MV_ShiftedWrite : MV_ShiftedRead;
	type |= ((unsigned long long)size << MV_SizeShift) & MV_SizeMask;
	type |= datatype << MV_DataShift;

	INS_InsertFillBuffer(ins, IPOINT_BEFORE, theBuffer,
		     IARG_MEMORYOP_EA, memOp, offsetof(struct BufferData, ea),
		     IARG_ADDRINT, type, offsetof(struct BufferData, type),
		     IARG_END);
    }
}

static void
imageEvent(IMG img, bool unload)
{
    THREADID	tid = PIN_ThreadId();

    MV_Header		header;
    MV_MMapType		type = MV_DATA;

    if (unload)
	type = MV_UNMAP;

    header.myType = MV_MMAP;
    header.myMMap.myStart = IMG_LowAddress(img);
    header.myMMap.myEnd = IMG_HighAddress(img);
    header.myMMap.myType = type;
    header.myMMap.myThread = tid;

    const char	*filename = IMG_Name(img).c_str();

    header.myMMap.mySize = strlen(filename)+1; // Include terminating '\0'

    PIN_GetLock(&theLock, tid);
    flushEvents();
    if (!write(KnobPipe, &header, sizeof(MV_Header)))
	;
    if (!write(KnobPipe, filename, header.myMMap.mySize))
	;
    PIN_ReleaseLock(&theLock);
}

// Pin calls this function every time a new img is loaded
// It can instrument the image, but this example does not
// Note that imgs (including shared libraries) are loaded lazily

VOID ImageLoad(IMG img, VOID *v)
{
    imageEvent(img, false);
}

// Pin calls this function every time a new img is unloaded
// You can't instrument an image that is about to be unloaded
VOID ImageUnload(IMG img, VOID *v)
{
    imageEvent(img, true);
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{
    flushEvents();

    fprintf(stderr, "Total events: %lld\n", theTotalEvents);
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Required for image callbacks to work
    PIN_InitSymbols();

    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    std::string shm = KnobSharedMem.Value();
    if (shm.empty())
	return 1;

    int shm_fd = open(shm.c_str(),
	    O_CLOEXEC | O_RDWR,
	    S_IRUSR | S_IWUSR);
    if (shm_fd == -1)
    {
	perror("shm_open");
	return 1;
    }

    theSharedData = (MV_SharedData *)mmap(NULL, sizeof(MV_SharedData),
	    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (theSharedData == MAP_FAILED)
    {
	perror("mmap");
	return 1;
    }

    theBlockIndex = 0;
    theBlock = &theSharedData->myData[theBlockIndex];
    theBlock->myEntries = 0;
    
    // Initialize the memory reference buffer;
    // set up the callback to process the buffer.
    //
    theBuffer = PIN_DefineTraceBuffer(
	    sizeof(BufferData), NUM_BUF_PAGES, BufferFull, 0);

    if(theBuffer == BUFFER_ID_INVALID)
    {
        cerr << "Error: could not allocate initial buffer" << endl;
        return 1;
    }

    INS_AddInstrumentFunction(Instruction, 0);

    // Register ImageLoad to be called when an image is loaded
    IMG_AddInstrumentFunction(ImageLoad, 0);

    // Register ImageUnload to be called when an image is unloaded
    IMG_AddUnloadFunction(ImageUnload, 0);
    
    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
