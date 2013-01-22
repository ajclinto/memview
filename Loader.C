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

#include "Loader.h"
#include "MemoryState.h"
#include "StackTraceMap.h"
#include "StopWatch.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define	SHARED_NAME "/memview"
#define	THREAD_LOADS

Loader::Loader(MemoryState *state, StackTraceMap *stack)
    : QThread(0)
    , myState(state)
    , myStackTrace(stack)
    , myTotalEvents(0)
    , myPendingClear(false)
    , myChild(-1)
    , myPipeFD(0)
    , myPipe(0)
    , mySharedData(0)
    , myIdx(0)
    , mySource(MEMVIEW_PIPE)
    , myAbort(false)
{
}

Loader::~Loader()
{
    myAbort = true;
    wait();

    // Lackey doesn't seem to have a SIGINT signal handler, so send it the
    // kill signal.
    if (myChild > 0)
	kill(myChild, SIGKILL);

    if (myPipe)
	fclose(myPipe);

    if (mySharedData)
	shm_unlink(SHARED_NAME);
}

bool
Loader::openPipe(int argc, char *argv[])
{
    const char	*tool = extractOption(argc, argv, "--tool=");
    const char	*valgrind = extractOption(argc, argv, "--valgrind=");

    // Check if we have a --tool argument.  This can override whether to
    // use lackey or the memview tool.
    if (tool)
    {
	if (!strcmp(tool, "lackey"))
	    mySource = LACKEY;
	else if (!strcmp(tool, "test"))
	    mySource = TEST;
    }

    // Allow overriden valgrind binary
    if (!valgrind)
	valgrind = "valgrind";

    if (mySource == TEST)
	return true;

    if (mySource == MEMVIEW_SHM)
    {
	int		shm_fd;

	// Set of shared memory before fork
	shm_fd = shm_open(SHARED_NAME,
		O_CREAT | O_CLOEXEC | O_RDWR,
		S_IRUSR | S_IWUSR);
	if (shm_fd == -1)
	{
	    fprintf(stderr, "could not get shared memory\n");
	    return false;
	}


	if (ftruncate(shm_fd, sizeof(MV_SharedData)) == -1)
	{
	    fprintf(stderr, "ftruncate failed\n");
	    return false;
	}

	mySharedData = (MV_SharedData *)mmap(NULL, sizeof(MV_SharedData),
		PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (mySharedData == MAP_FAILED)
	{
	    fprintf(stderr, "mmap failed\n");
	    return false;
	}

	memset(mySharedData, 0, sizeof(MV_SharedData));
    }

    int		fd[2];
    if (pipe(fd) < 0)
    {
	perror("pipe failed");
	return false;
    }

    myChild = fork();
    if (myChild == -1)
    {
	perror("fork failed");
	return false;
    }

    if (myChild == 0)
    {
	// Close input for child
	close(fd[0]);

	static const int	 theMaxArgs = 256;
	const char		*args[theMaxArgs];
	int			 vg_args = 0;

	args[vg_args++] = valgrind;
	if (mySource != LACKEY)
	{
	    args[vg_args++] = "--tool=memview";
	    if (mySource == MEMVIEW_SHM)
	    {
		args[vg_args++] = "--shared-mem=/dev/shm" SHARED_NAME;
	    }
	    else
	    {
		char	pipearg[64];
		sprintf(pipearg, "--pipe=%d", fd[1]);
		args[vg_args++] = pipearg;
	    }
	}
	else
	{
	    // Copy stderr to the output of the pipe
	    dup2(fd[1], 2);

	    args[vg_args++] = "--tool=lackey";
	    args[vg_args++] = "--basic-counts=no";
	    args[vg_args++] = "--trace-mem=yes";
	}
	for (int i = 0; i < argc; i++)
	    args[i+vg_args] = argv[i];
	args[argc+vg_args] = NULL;

	if (execvp(valgrind, (char * const *)args) == -1)
	{
	    char    buf[256];
	    sprintf(buf, "Could not execute %s", valgrind);
	    perror(buf);
	    return false;
	}
	// Unreachable
    }

    // Close output for parent
    close(fd[1]);

    // Open the pipe for reading
    myPipeFD = fd[0];
    myPipe = fdopen(myPipeFD, "r");

    // Start a timer to increment the access time counter.  This timer runs
    // faster than the display timer since it's with this higher resolution
    // it's possible to see gradation in access times within the timestep.
    startTimer(10);

    return true;
}

void
Loader::run()
{
    //StopWatch timer;
    myAbort = false;
    while (!myAbort)
    {
	bool	rval = false;

	MemoryState	*pending = 0;
	bool		 pendingclear = false;

	{
	    QMutexLocker lock(&myPendingLock);
	    pending = myPendingState.release();
	    pendingclear = pendingclear;
	    myPendingClear = false;
	}

	if (pendingclear)
	    myZoomState.reset();

	if (pending)
	{
	    // Threads might be writing to the state or zoom state.  Wait
	    // for these to finish to we have a consistent state to
	    // downsample.
#ifdef THREAD_LOADS
	    QThreadPool::globalInstance()->waitForDone();
#endif

	    // Ensure that we clean up the zoom state
	    MemoryStateHandle zoom(myZoomState);

	    myZoomState.reset(pending);

	    // This could take a while
	    if (zoom && zoom->getIgnoreBits() <
		    myZoomState->getIgnoreBits())
		myZoomState->downsample(*zoom);
	    else
		myZoomState->downsample(*myState);
	}

	switch (mySource)
	{
	    case TEST:
		rval = loadFromTest();
		break;
	    case LACKEY:
		rval = loadFromLackey(MV_BlockSize);
		break;
	    case MEMVIEW_PIPE:
		rval = loadFromPipe();
		break;
	    case MEMVIEW_SHM:
		rval = loadFromSharedMemory();
		break;
	}
	if (!rval)
	    myAbort = true;
    }

#ifdef THREAD_LOADS
    QThreadPool::globalInstance()->waitForDone();
#endif
}

bool
Loader::loadFromLackey(int max_read)
{
    if (!myPipe)
	return false;

    char	*buf = 0;
    size_t	 n = 0;

    for (int i = 0; i < max_read; i++)
    {
	if (getline(&buf, &n, myPipe) <= 0)
	{
	    pclose(myPipe);
	    myPipe = 0;
	    return false;
	}

	uint64		addr;
	uint64		size;
	uint64		type;

	char		*saveptr = 0;
	char		*type_str;
	char		*addr_str;
	char		*size_str;
	const char	*delim = " ,\n";

	type_str = strtok_r(buf, delim, &saveptr);
	if (!type_str)
	    continue;

	switch (type_str[0])
	{
	    case 'L': type = MV_TypeRead; break;
	    case 'S':
	    case 'M': type = MV_TypeWrite; break;
	    case 'I': type = MV_TypeInstr; break;
	    default: continue;
	}

	addr_str = strtok_r(0, delim, &saveptr);
	if (!addr_str)
	    continue;
	addr = strtoull(addr_str, 0, 16); // Hex value

	size_str = strtok_r(0, delim, &saveptr);
	if (!size_str)
	    continue;
	size = atoi(size_str);

	if (strtok_r(0, delim, &saveptr))
	    continue;

	myState->updateAddress(addr, size, type);
	if (myZoomState)
	    myZoomState->updateAddress(addr, size, type);
    }

    myTotalEvents += max_read;

    if (buf)
	free(buf);

    return max_read;
}

static inline void
decodeAddr(uint64 &addr, uint64 &size, uint64 &type)
{
    size = addr >> MV_SizeShift,
    type = (addr & MV_TypeMask) >> MV_TypeShift;
    addr &= MV_AddrMask;
}

bool
Loader::loadFromPipe()
{
    if (!myPipe)
	return false;

    TraceBlockHandle block(new MV_TraceBlock);

    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(myPipeFD, &rfds);

    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    // Waits for data to be ready or 0.1s
    int retval = select(myPipeFD+1, &rfds, NULL, NULL, &tv);

    if (retval == -1)
    {
	perror("select failed");
	return false;
    }

    if (retval == 0)
	return true;

    MV_Header header;
    if (!read(myPipeFD, &header, sizeof(MV_Header)))
	return false;

    if (header.myType == MV_BLOCK)
    {
	if (read(myPipeFD, block.get(), sizeof(MV_TraceBlock)))
	{
	    if (block->myEntries && loadBlock(block))
		return true;
	}
    }
    else if (header.myType == MV_STACKTRACE)
    {
	MV_StackInfo stack;
	if (read(myPipeFD, &stack, header.mySize))
	{
	    uint64 size, type;
	    decodeAddr(stack.myAddr, size, type);
	    myStackTrace->insert(stack.myAddr, stack.myBuf);
	    return true;
	}
    }
    else if (header.myType == MV_MMAP)
    {
	MV_MMapInfo info;
	char	    buf[MV_STR_BUFSIZE];
	if (read(myPipeFD, &info, sizeof(MV_MMapInfo)))
	    if (read(myPipeFD, buf, header.mySize-sizeof(MV_MMapInfo)))
		return true;
    }

    return false;
}

bool
Loader::loadFromSharedMemory()
{
    fprintf(stderr, "Shared memory currently unsupported\n");
    return false;
}

bool
Loader::loadFromTest()
{
    static const uint64 theSize = 32*1024;
    static uint64 theCount = 0;

    TraceBlockHandle	block(new MV_TraceBlock);
    block->myEntries = MV_BlockSize;
    for (uint64 j = 0; j < block->myEntries; j++)
    {
	block->myAddr[j] = theCount*MV_BlockSize + j;
	block->myAddr[j] |= (uint64)MV_TypeRead << MV_TypeShift;
	block->myAddr[j] |= (uint64)4 << MV_SizeShift;
    }
    loadBlock(block);

    theCount++;
    if (theCount >= theSize)
	theCount = 0;

    return theCount > 0;
}

template <typename HandleType>
class UpdateState : public QRunnable {
public:
    UpdateState(HandleType &state, const TraceBlockHandle &block)
	: myState(state)
	, myBlock(block) {}

    virtual void run()
    {
	QMutexLocker lock(myState->writeLock());

	uint32 count = myBlock->myEntries;
	for (uint32 i = 0; i < count; i++)
	{
	    uint64 addr = myBlock->myAddr[i];
	    uint64 size, type;
	    decodeAddr(addr, size, type);
	    myState->updateAddress(addr, size, type);
	}
    }

private:
    HandleType	     myState;
    TraceBlockHandle myBlock;
};

#ifdef THREAD_LOADS
static void
addToPool(QThreadPool *pool, QRunnable *task)
{
    if (!pool->tryStart(task))
    {
	task->run();
	delete task;
    }
}
#endif

bool
Loader::loadBlock(const TraceBlockHandle &block)
{
    // Basic semantic checking to ensure we received valid data
    uint64 type = (block->myAddr[0] & MV_TypeMask) >> MV_TypeShift;
    if (block->myEntries > MV_BlockSize || type > 7)
    {
	fprintf(stderr, "received invalid block (size %u, type %lld)\n",
		block->myEntries, type);
	return false;
    }

#ifdef THREAD_LOADS
    auto pool = QThreadPool::globalInstance();
    QRunnable *task = new UpdateState<MemoryState *>(myState, block);
    addToPool(pool, task);

    if (myZoomState)
    {
	task = new UpdateState<MemoryStateHandle>(myZoomState, block);
	addToPool(pool, task);
    }
#else
    UpdateState<MemoryState *> state(myState, block);
    state.run();

    if (myZoomState)
    {
	UpdateState<MemoryStateHandle> zoomstate(myZoomState, block);
	zoomstate.run();
    }
#endif

    myTotalEvents += block->myEntries;

    return true;
}

void
Loader::timerEvent(QTimerEvent *)
{
    if (myZoomState)
    {
	QMutexLocker lock(myZoomState->writeLock());
	myZoomState->incrementTime();
    }

    QMutexLocker lock(myState->writeLock());
    myState->incrementTime();
}

