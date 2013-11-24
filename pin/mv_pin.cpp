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

/* ================================================================== */
// Global variables 
/* ================================================================== */

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

static inline void
recordEvent(void *addr, unsigned long long type)
{
    unsigned long long val;
    val = (unsigned long long)addr & MV_AddrMask;
    val |= type;
    theBlock->myAddr[theBlock->myEntries] = val;
    theBlock->myEntries++;
    if (theBlock->myEntries >= theMaxEntries)
	flushEvents();
}

// Print a memory read record
VOID RecordMemRead(VOID * ip, VOID * addr)
{
    recordEvent(addr, MV_ShiftedRead);
}

// Print a memory write record
VOID RecordMemWrite(VOID * ip, VOID * addr)
{
    recordEvent(addr, MV_ShiftedWrite);
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
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
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
    if (theBlock->myEntries)
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
    
    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    INS_AddInstrumentFunction(Instruction, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
