#ifndef MV_IPC_H
#define MV_IPC_H

//
// The unidirectional (tool to memview) pipe communication protocol is
// simple.  First a message header is sent, followed by the data.  The size
// of the data is specified in the header.
//

// Message types
typedef enum {
    MV_BLOCK,
    MV_STACKTRACE
} MessageType;

typedef struct {
    int	    mySize;
    int	    myType;
} Header;

#define MV_STACKTRACE_BUFSIZE 4096

#define theBlockSize	(1024*32)

#define theAddrMask 0x001FFFFFFFFFFFFFul

#define theSizeMask 0xFF00000000000000ul
#define theSizeShift 56

#define theTypeMask 0x00E0000000000000ul
#define theTypeShift 53

// Order is important here - we use a max() for downsampling, which will
// cause reads to be preferred over writes when the event time matches.  If
// you update these values, you will also need to update the shader.frag
// code.
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
} TraceBlock;

// Template for future work
typedef struct {} SharedData;

#endif

