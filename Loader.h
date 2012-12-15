#ifndef Loader_H
#define Loader_H

#include <QtGui>
#include <sys/types.h>
#include <signal.h>
#include "tool/mv_ipc.h"
#include "Math.h"

class MemoryState;

class Loader : public QThread {
public:
     Loader(MemoryState *state);
    ~Loader();

    bool	openPipe(int argc, char *argv[]);

    void	setZoomState(MemoryState *state)
		{ myPendingState = state; }
    void	clearZoomState()
		{ myPendingClear = true; }

    MemoryState	*getBaseState() const { return myState; }

protected:
    void	run();

private:
    bool	loadFromLackey(int max_read);
    bool	loadFromPipe();
    bool	loadFromSharedMemory();
    bool	loadFromTest();

    bool	loadBlock(const TraceBlock &block);

private:
    MemoryState	*myState;
    MemoryState	*myZoomState;
    MemoryState	*myPendingState;
    bool	 myPendingClear;

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

// Extract the option with the given prefix, removing it from argc/argv.
static const char *
extractOption(int &argc, char *argv[], const char *prefix)
{
    const int	len = strlen(prefix);
    for (int i = 0; i < argc; i++)
    {
	if (!strncmp(argv[i], prefix, len))
	{
	    const char	*opt = argv[i] + len;
	    // Shorten the argument list
	    argc--;
	    for (; i < argc; i++)
		argv[i] = argv[i+1];
	    return opt;
	}
    }
    return 0;
}

#endif
