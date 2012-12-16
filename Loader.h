#ifndef Loader_H
#define Loader_H

#include <QtGui>
#include "tool/mv_ipc.h"
#include "Math.h"
#include <memory>
#include <sys/types.h>
#include <signal.h>

class MemoryState;

typedef std::shared_ptr<MemoryState> MemoryStateHandle;
typedef std::shared_ptr<TraceBlock> TraceBlockHandle;

class Loader : public QThread {
public:
     Loader(MemoryState *state);
    ~Loader();

    bool	openPipe(int argc, char *argv[]);

    void	setZoomState(MemoryState *state)
		{
		    QMutexLocker lock(&myPendingLock);
		    myPendingState.reset(state);
		}
    void	clearZoomState()
		{
		    QMutexLocker lock(&myPendingLock);
		    myPendingClear = true;
		}

    MemoryState	*getBaseState() const { return myState; }

protected:
    void	run();

private:
    bool	loadFromLackey(int max_read);
    bool	loadFromPipe();
    bool	loadFromSharedMemory();
    bool	loadFromTest();

    bool	loadBlock(const TraceBlockHandle &block);

private:
    MemoryState		*myState;
    MemoryStateHandle	 myZoomState;

    QMutex		 myPendingLock;
    std::unique_ptr<MemoryState> myPendingState;
    bool		 myPendingClear;

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
