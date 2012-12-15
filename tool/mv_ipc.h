#ifndef MV_IPC_H
#define MV_IPC_H

#define theBlockSize	(1024*32)
#define theBlockCount	4

#define theAddrMask 0x001FFFFFFFFFFFFFul

#define theSizeMask 0xFF00000000000000ul
#define theSizeShift 56

#define theTypeMask 0x00E0000000000000ul
#define theTypeShift 53

// Order is important here - we use a max() for downsampling, which will
// cause reads to be preferred over writes when the event time matches.  If
// you update these values, you will also need to update the shader.frag
// code to handle it.
#define theTypeAlloc  0
#define theTypeInstr  1
#define theTypeWrite  2
#define theTypeRead   3
#define theTypeFree   4

#define theShiftedAlloc  ((unsigned long long)theTypeAlloc << theTypeShift)
#define theShiftedInstr  ((unsigned long long)theTypeInstr << theTypeShift)
#define theShiftedWrite  ((unsigned long long)theTypeWrite << theTypeShift)
#define theShiftedRead   ((unsigned long long)theTypeRead << theTypeShift)
#define theShiftedFree   ((unsigned long long)7 << theTypeShift)

typedef struct {
    unsigned long long	myAddr[theBlockSize];
    unsigned int	myEntries;
    volatile int	myWSem;
    volatile int	myRSem;
} TraceBlock;

typedef struct {
    TraceBlock	myBlocks[theBlockCount];
} SharedData;

#endif

