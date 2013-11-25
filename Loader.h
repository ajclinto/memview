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

#ifndef Loader_H
#define Loader_H

#include <QThread>
#include "mv_ipc.h"
#include "Math.h"
#include "IntervalMap.h"
#include <unordered_map>
#include <memory>
#include <sys/types.h>
#include <signal.h>

class MemoryState;

// This is a MV_TraceBlock that has been dynamically allocated to eliminate
// wasted space.
class LoaderBlock {
public:
    LoaderBlock(uint32 size)
	: myEntries(size)
    {
	myAddr = new uint64[size];
    }
    LoaderBlock(const MV_TraceBlock &block)
	: myEntries(block.myEntries)
    {
	myAddr = new uint64[myEntries];
	memcpy(myAddr, block.myAddr, myEntries*sizeof(uint64));
    }
    ~LoaderBlock() { delete [] myAddr; }

    uint64	*myAddr;
    uint32	 myEntries;
};

typedef std::shared_ptr<MemoryState> MemoryStateHandle;
typedef std::shared_ptr<LoaderBlock> LoaderBlockHandle;

class Loader : public QThread {
public:
     Loader(MemoryState *state,
	    StackTraceMap *stack,
	    MMapMap *mmapmap,
	    const std::string &path);
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

    // Regulates the interval between stack traces
    void	setBlockSize(int size)
		{
		    myBlockSize = SYSclamp(size, 1, MV_BlockSize);
		}

    MemoryState	*getBaseState() const { return myState; }

    uint64	getTotalEvents() const { return myTotalEvents; }
    bool	isComplete() const { return myAbort; }

    pid_t	getChild() const { return myChild; }

protected:
    void	run();

private:
    bool	initSharedMemory();
    void	writeToken(int token);
    bool	waitForInput(int timeout_ms);
    bool	loadFromLackey(int max_read);
    bool	loadFromPipe();
    bool	loadFromSharedMemory();

    template <bool with_stacks>
    bool	loadFromTest();

    bool	loadBlock(const LoaderBlockHandle &block);
    void	loadMMap(const MV_Header &header, const char *buf);

    void	timerEvent(QTimerEvent *event);

private:
    typedef std::unordered_map<std::string, int> MMapNameMap;

    MemoryState		*myState;
    MemoryStateHandle	 myZoomState;
    StackTraceMap	*myStackTrace;
    MMapMap		*myMMapMap;
    MMapNameMap		 myMMapNames;
    uint64		 myTotalEvents;
    std::string		 myPath;

    QMutex		 myPendingLock;
    std::unique_ptr<MemoryState> myPendingState;
    bool		 myPendingClear;

    int			 myBlockSize;

    // Child process
    pid_t	 myChild;
    int		 myPipeFD;
    FILE	*myPipe;
    int		 myOutPipeFD;
    FILE	*myOutPipe;

    std::string		 mySharedName;
    MV_SharedData	*mySharedData;
    int			 myIdx;
    int			 myNextToken;

    // What are we loading from?
    enum LoadSource {
	NONE,
	LACKEY,
	MEMVIEW_PIPE,
	PIN,
	TEST
    };

    LoadSource	 mySource;
    int		 myTestType;
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
