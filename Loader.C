#include "Loader.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "MemoryState.h"

#define	SHARED_NAME "/memview"

Loader::Loader(MemoryState *state)
    : QThread(0)
    , myState(state)
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


	if (ftruncate(shm_fd, sizeof(SharedData)) == -1)
	{
	    fprintf(stderr, "ftruncate failed\n");
	    return false;
	}

	mySharedData = (SharedData *)mmap(NULL, sizeof(SharedData),
		PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (mySharedData == MAP_FAILED)
	{
	    fprintf(stderr, "mmap failed\n");
	    return false;
	}

	memset(mySharedData, 0, sizeof(SharedData));
	for (int i = 0; i < theBlockCount; i++)
	    mySharedData->myBlocks[i].myWSem = 1;
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

    return true;
}

void
Loader::run()
{
    while (!myAbort)
    {
	bool	rval = false;
	switch (mySource)
	{
	    case TEST:
		rval = loadFromTest();
		break;
	    case LACKEY:
		rval = loadFromLackey(theBlockSize);
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
}

bool
Loader::loadFromLackey(int max_read)
{
    int		 i;

    if (!myPipe)
	return false;

    char	*buf = 0;
    size_t	 n = 0;

    for (i = 0; i < max_read; i++)
    {
	if (getline(&buf, &n, myPipe) <= 0)
	{
	    pclose(myPipe);
	    myPipe = 0;
	    return false;
	}

	uint64		addr;
	int		size;
	int		type;

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
	    case 'L': type = theTypeRead; break;
	    case 'S':
	    case 'M': type = theTypeWrite; break;
	    case 'I': type = theTypeInstr; break;
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
    }

    if (i > 0)
	myState->incrementTime();

    if (buf)
	free(buf);

    return i > 0;
}

bool
Loader::loadFromPipe()
{
    if (!myPipe)
	return false;

    TraceBlock	block;

    if (read(myPipeFD, &block, sizeof(TraceBlock)))
    {
	// Basic semantic checking to ensure we received valid data
	if (block.myEntries)
	{
	    int type = (block.myAddr[0] & theTypeMask) >> theTypeShift;
	    if (type > 7)
	    {
		fprintf(stderr, "received invalid block (size %d)\n",
			block.myEntries);
		return false;
	    }

	    for (int j = 0; j < block.myEntries; j++)
	    {
		unsigned long long addr = block.myAddr[j];
		myState->updateAddress(
			addr & theAddrMask,
			addr >> theSizeShift,
			(addr & theTypeMask) >> theTypeShift);
	    }

	    myState->incrementTime();
	}

	return block.myEntries == theBlockSize;
    }

    return false;
}

bool
Loader::loadFromSharedMemory()
{
    if (!mySharedData)
	return false;

    TraceBlock	&block = mySharedData->myBlocks[myIdx];
    while (!block.myRSem)
	;
    block.myRSem = 0;

    int		count = block.myEntries;
    if (count)
    {
	// Basic semantic checking to ensure we received valid data
	int type = (block.myAddr[0] & theTypeMask) >> theTypeShift;
	if (type > 7)
	{
	    fprintf(stderr, "received invalid block (size %d)\n", count);
	    return false;
	}

	for (int i = 0; i < count; i++)
	{
	    unsigned long long addr = block.myAddr[i];
	    myState->updateAddress(
		    addr & theAddrMask,
		    addr >> theSizeShift,
		    (addr & theTypeMask) >> theTypeShift);
	}

	myState->incrementTime();
    }

    block.myWSem = 1;
    myIdx++;
    if (myIdx == theBlockCount)
	myIdx = 0;

    // If it wasn't a full block, we're at the end of the stream.
    return count == theBlockSize;
}

bool
Loader::loadFromTest()
{
    static const uint64 theSize = 16*1024;
    static uint64 theCount = 0;

    for (uint64 j = 0; j < 1024; j++)
	//myState->updateAddress(theCount*16*1024 + j, 4, theTypeRead);
	myState->updateAddress(theCount*1024 + j, 4, theTypeRead);

    myState->incrementTime();
    theCount++;

    return theCount < theSize;
}

