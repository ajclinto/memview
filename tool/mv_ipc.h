#ifndef MV_IPC_H
#define MV_IPC_H

#define theBlockSize	(1024*32)
#define theBlockCount	4

typedef unsigned char AccessSize;

typedef struct {
    unsigned long long	myAddr[theBlockSize];
    char		myType[theBlockSize];
    AccessSize		mySize[theBlockSize];
    int			myEntries;
    volatile int	myWSem;
    volatile int	myRSem;
} TraceBlock;

typedef struct {
    TraceBlock	myBlocks[theBlockCount];
} SharedData;

#endif

