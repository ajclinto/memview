#ifndef Loader_H
#define Loader_H

#include <QtGui>
#include <sys/types.h>
#include <signal.h>
#include "Math.h"

class MemoryState;

class Loader : public QThread {
public:
     Loader(MemoryState *state);
    ~Loader();

    bool	openPipe(int argc, char *argv[]);

protected:
    void	run();

private:
    bool	loadFromLackey(int max_read);
    bool	loadFromPipe();
    bool	loadFromSharedMemory();

private:
    static const int	theBlockSize = 1024*16;
    static const int	theBlockCount = 4;

    typedef struct {
	uint64		myAddr[theBlockSize];
	char		myType[theBlockSize];
	char		mySize[theBlockSize];
	int		myEntries;
	volatile int	myWSem;
	volatile int	myRSem;
    } TraceBlock;

    typedef struct {
	TraceBlock	myBlocks[theBlockCount];
    } SharedData;

    MemoryState	*myState;

    // Child process
    pid_t	 myChild;
    int		 myPipeFD;
    FILE	*myPipe;

    // Shared memory
    SharedData	*mySharedData;
    int		 myIdx;

    // What are we loading from?
    enum LoadSource {
	LACKEY,
	MEMVIEW_PIPE,
	MEMVIEW_SHM
    };

    LoadSource	 mySource;
    bool	 myAbort;
};

#endif
