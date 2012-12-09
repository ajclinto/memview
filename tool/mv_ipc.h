#ifndef MV_IPC_H
#define MV_IPC_H

#define theBlockSize	(1024*32)
#define theBlockCount	4

#define theAddrMask 0x001FFFFFFFFFFFFFul

#define theSizeMask 0xFF00000000000000ul
#define theSizeShift 56

#define theTypeMask 0x00E0000000000000ul
#define theTypeShift 53

#define theTypeRead   0
#define theTypeWrite  1
#define theTypeInstr  2
#define theTypeAlloc  3
#define theTypeFree   4

#define theShiftedRead   (0ul << theTypeShift)
#define theShiftedWrite  (1ul << theTypeShift)
#define theShiftedInstr  (2ul << theTypeShift)
#define theShiftedAlloc  (3ul << theTypeShift)
#define theShiftedFree   (7ul << theTypeShift)

typedef struct {
    unsigned long long	myAddr[theBlockSize];
    int			myEntries;
    volatile int	myWSem;
    volatile int	myRSem;
} TraceBlock;

typedef struct {
    TraceBlock	myBlocks[theBlockCount];
} SharedData;

#endif

