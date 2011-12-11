#ifndef Loader_H
#define Loader_H

#include <QtGui>
#include <sys/types.h>
#include <signal.h>
#include "mv_ipc.h"
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
    bool	loadFromTest();

private:
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
	MEMVIEW_SHM,
	TEST
    };

    LoadSource	 mySource;
    bool	 myAbort;
};

#endif
