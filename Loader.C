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
#include "StopWatch.h"
#include <QThreadPool>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>

#define	SHARED_NAME "/memview"
#define	THREAD_LOADS

Loader::Loader(MemoryState *state,
	       StackTraceMap *stack,
	       MMapMap *mmapmap,
	       const std::string &path)
    : QThread(0)
    , myState(state)
    , myStackTrace(stack)
    , myMMapMap(mmapmap)
    , myTotalEvents(0)
    , myPath(path)
    , myPendingClear(false)
    , myBlockSize(MV_BlockSize)
    , myChild(-1)
    , myPipeFD(0)
    , myPipe(0)
    , myOutPipeFD(0)
    , myOutPipe(0)
    , mySharedData(0)
    , myIdx(0)
    , mySource(NONE)
    , myTestType(0)
    , myAbort(false)
{
    // Start a timer to increment the access time counter.  This timer runs
    // faster than the display timer since with this higher resolution it's
    // possible to see gradation in access times within a single frame.
    startTimer(10);
}

Loader::~Loader()
{
    myAbort = true;
    wait();

    // Lackey doesn't seem to have a SIGINT signal handler, so send it the
    // kill signal.
    if (myChild > 0)
	kill(myChild, SIGKILL);

    if (myPipe) fclose(myPipe);
    if (myOutPipe) fclose(myOutPipe);

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
    mySource = MEMVIEW_PIPE;
    if (tool)
    {
	if (!strcmp(tool, "lackey"))
	    mySource = LACKEY;
	else if (!strcmp(tool, "test"))
	    mySource = TEST;
	else if (!strcmp(tool, "teststack"))
	{
	    mySource = TEST;
	    myTestType = 1;
	}
    }

    // Allow overriden valgrind binary
    if (!valgrind)
	valgrind = "valgrind";

    if (mySource == TEST)
	return true;

    int		fd[2];
    int		outfd[2];

    if (pipe(fd) < 0)
    {
	perror("pipe failed");
	return false;
    }
    if (pipe(outfd) < 0)
    {
	perror("pipe failed");
	return false;
    }

    if (!initSharedMemory())
	return false;

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
	close(outfd[1]);

	static const int	 theMaxArgs = 256;
	const char		*args[theMaxArgs];
	char			 pipearg[64];
	char			 outpipearg[64];
	int			 vg_args = 0;

	args[vg_args++] = valgrind;
	if (mySource != LACKEY)
	{
	    args[vg_args++] = "--tool=memview";

	    if (mySharedData)
		args[vg_args++] = "--shared-mem=/dev/shm" SHARED_NAME;

	    sprintf(pipearg, "--pipe=%d", fd[1]);
	    args[vg_args++] = pipearg;

	    sprintf(outpipearg, "--inpipe=%d", outfd[0]);
	    args[vg_args++] = outpipearg;
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

	if (myPath != "/usr/bin/")
	{
	    // If the executable is not executing from the install
	    // directory, look for valgrind in the source tree.
	    std::string valgrind_dir =
		myPath + "valgrind/valgrind_src/.in_place";

	    setenv("VALGRIND_LIB", valgrind_dir.c_str(), 1);
	}

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
    close(outfd[0]);

    // Open the pipe for reading
    myPipeFD = fd[0];
    myPipe = fdopen(myPipeFD, "r");

    // Open the pipe for writing
    myOutPipeFD = outfd[1];
    myOutPipe = fdopen(myOutPipeFD, "w");

    // Queue up some tokens
    myNextToken = 1;
    for (int i = 1; i < MV_BufCount; i++)
	writeToken(myBlockSize);

    return true;
}

bool
Loader::initSharedMemory()
{
    // Set of shared memory before fork
    int shm_fd = shm_open(SHARED_NAME,
	    O_CREAT | O_CLOEXEC | O_RDWR,
	    S_IRUSR | S_IWUSR);
    if (shm_fd == -1)
    {
	perror("shm_open");
	return false;
    }

    if (ftruncate(shm_fd, sizeof(MV_SharedData)) == -1)
    {
	perror("ftruncate");
	return false;
    }

    mySharedData = (MV_SharedData *)mmap(NULL, sizeof(MV_SharedData),
	    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mySharedData == MAP_FAILED)
    {
	perror("mmap");
	return false;
    }

    memset(mySharedData, 0, sizeof(MV_SharedData));
    return true;
}

static void
incBuf(int &idx)
{
    idx++;
    if (idx == MV_BufCount)
	idx = 0;
}

void
Loader::writeToken(int token)
{
    if (write(myOutPipeFD, &token, sizeof(int)))
	incBuf(myNextToken);
}

bool
Loader::waitForInput(int timeout_ms)
{
    fd_set	rfds;
    int		max_fd = 0;

    FD_ZERO(&rfds);
    if (myPipe)
    {
	FD_SET(myPipeFD, &rfds);
	max_fd = myPipeFD + 1;
    }

    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 1000*timeout_ms;

    // Waits for data to be ready or timeout_ms
    int retval = select(max_fd, &rfds, NULL, NULL, &tv);

    if (retval == -1)
    {
	perror("select failed");
	return false;
    }

    // Return true when there's input available
    return retval > 0;
}

void
Loader::run()
{
    //StopWatch timer;
    while (!myAbort)
    {
	MemoryState	*pending = 0;
	bool		 pendingclear = false;

	{
	    QMutexLocker lock(&myPendingLock);
	    pending = myPendingState.release();
	    pendingclear = myPendingClear;
	    myPendingClear = false;
	}

	if (pendingclear)
	    myZoomState.reset();

	if (pending)
	{
	    // Threads might be writing to the state or zoom state.  Wait
	    // for these to finish so we have a consistent state to
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

	const int   timeout_ms = 50;
	bool	    rval = true;
	switch (mySource)
	{
	    case NONE:
		waitForInput(timeout_ms);
		break;
	    case TEST:
		if (myTestType)
		    rval = loadFromTest<true>();
		else
		    rval = loadFromTest<false>();
		break;
	    case LACKEY:
		if (waitForInput(timeout_ms))
		    rval = loadFromLackey(MV_BlockSize);
		break;
	    case MEMVIEW_PIPE:
		if (waitForInput(timeout_ms))
		    rval = loadFromPipe();
		break;
	}

	// Input has completed.  We'll still loop to handle zoom requests
	if (!rval)
	    mySource = NONE;
    }

#ifdef THREAD_LOADS
    QThreadPool::globalInstance()->waitForDone();
#endif
}

static inline void
decodeAddr(uint64 addr, uint64 &size, uint64 &type)
{
    size = (addr & MV_SizeMask) >> MV_SizeShift,
    type = addr >> MV_DataShift;
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
	    case 'L': type = MV_ShiftedRead; break;
	    case 'S':
	    case 'M': type = MV_ShiftedWrite; break;
	    case 'I': type = MV_ShiftedInstr; break;
	    default: continue;
	}

	addr_str = strtok_r(0, delim, &saveptr);
	if (!addr_str)
	    continue;
	addr = strtoull(addr_str, 0, 16); // Hex value
	addr &= MV_AddrMask;

	size_str = strtok_r(0, delim, &saveptr);
	if (!size_str)
	    continue;
	size = atoi(size_str);

	if (strtok_r(0, delim, &saveptr))
	    continue;

	// Set the data type
	if (size < 4)
	    type |= (uint64)MV_DataChar8 << MV_DataShift;
	else if (size > 4)
	    type |= (uint64)MV_DataInt64 << MV_DataShift;
	else
	    type |= (uint64)MV_DataInt32 << MV_DataShift;

	size <<= MV_SizeShift;

	// Set the thread id to thread 1
	type |= 1ull << MV_ThreadShift;

	addr |= size | type;
	decodeAddr(addr, size, type);
	addr &= MV_AddrMask;

	myState->updateAddress(addr, size, type);
	if (myZoomState)
	    myZoomState->updateAddress(addr, size, type);
    }

    myTotalEvents += max_read;

    if (buf)
	free(buf);

    return max_read;
}

bool
Loader::loadFromPipe()
{
    if (!myPipe)
	return false;

    MV_Header header;
    if (!read(myPipeFD, &header, sizeof(MV_Header)))
	return false;

    if (header.myType == MV_BLOCK)
    {
	LoaderBlockHandle block(new LoaderBlock(mySharedData->myData[myIdx]));

	writeToken(myBlockSize);

	incBuf(myIdx);

	if (block->myEntries && loadBlock(block))
	    return true;
    }
    else if (header.myType == MV_STACKTRACE)
    {
	char	stack[MV_STR_BUFSIZE];
	if (read(myPipeFD, stack, header.myStack.mySize))
	{
	    uint64 addr, size, type;
	    addr = header.myStack.myAddr;
	    decodeAddr(addr, size, type);
	    addr &= MV_AddrMask;

	    MemoryState::State	state;
	    state.init(myState->getTime(), type);

	    StackTraceMapWriter writer(*myStackTrace);
	    writer.insert(addr, addr + size, StackInfo{stack, state.uval});
	    return true;
	}
    }
    else if (header.myType == MV_MMAP)
    {
	char	    buf[MV_STR_BUFSIZE];
	if (read(myPipeFD, buf, header.myMMap.mySize))
	{
	    loadMMap(header, buf);
	    return true;
	}
    }

    return false;
}

static inline void
appendBuf(std::string &str, const char *buf)
{
    if (buf && buf[0])
    {
	str += "(";
	str += buf;
	str += ")";
    }
}

class Func {
public:
    void operator()(MMapInfo &val) const { val.myMapped = false; }
};

void
Loader::loadMMap(const MV_Header &header, const char *buf)
{
    MMapMapWriter writer(*myMMapMap);
    if (header.myMMap.myType != MV_UNMAP)
    {
	std::string	info;
	switch (header.myMMap.myType)
	{
	    case MV_CODE:
		info = "Code";
		appendBuf(info, buf);
		break;
	    case MV_DATA:
		info = "Data";
		appendBuf(info, buf);
		break;
	    case MV_HEAP: info = "Heap"; break;
	    case MV_STACK:
		info = "Thread ";
#if HAS_LAMBDA
		info += std::to_string(header.myMMap.myThread);
#else
		{
		    std::ostringstream os;
		    os << header.myMMap.myThread;
		    info += os.str();
		}
#endif
		info += " stack";
		break;
	    case MV_SHM:
		info = "Shared";
		appendBuf(info, buf);
		break;
	    case MV_UNMAP: break;
	}

	// Create an integer index for each unique mmap string
	int &idx = myMMapNames[info];
	if (!idx)
	    idx = (int)myMMapNames.size();

	writer.insert(
		header.myMMap.myStart,
		header.myMMap.myEnd, MMapInfo{info,idx,true});
    }
    else
    {
	writer.apply(
		header.myMMap.myStart,
		header.myMMap.myEnd,
#if HAS_LAMBDA
		[] (MMapInfo &val) { val.myMapped = false; }
#else
		Func()
#endif
		);
    }
}

bool
Loader::loadFromSharedMemory()
{
    fprintf(stderr, "Shared memory currently unsupported\n");
    return false;
}

template <bool with_stacks>
bool
Loader::loadFromTest()
{
    static const uint64 theSize = 8*1024;
    static const uint64 theStackRate = 63;
    static const uint64 theTypeInfo = ((uint64)MV_DataInt32 << MV_DataShift)
				    | ((uint64)MV_TypeRead << MV_TypeShift)
				    | ((uint64)4 << MV_SizeShift);

    static uint64 theCount = 0;

    if (theCount >= theSize)
	return false;

    LoaderBlockHandle	block(new LoaderBlock(MV_BlockSize));
    for (uint32 j = 0; j < MV_BlockSize; j++)
    {
	block->myAddr[j] = (theCount*MV_BlockSize + j) << 2;
	block->myAddr[j] |= theTypeInfo;

	// Insert a stack
	if (with_stacks && !(j & theStackRate))
	{
	    uint64 addr, size, type;
	    addr = block->myAddr[j];
	    decodeAddr(addr, size, type);
	    addr &= MV_AddrMask;

	    StackTraceMapWriter writer(*myStackTrace);
	    writer.insert(addr, addr + size,
		    StackInfo{"", myState->getTime()});
	}
    }
    loadBlock(block);

    theCount++;
    return true;
}

template <typename HandleType>
class UpdateState : public QRunnable {
public:
    UpdateState(HandleType &state, const LoaderBlockHandle &block)
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
    LoaderBlockHandle myBlock;
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
Loader::loadBlock(const LoaderBlockHandle &block)
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
    myState->incrementTime(myStackTrace);
}

