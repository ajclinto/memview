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
    , myPipe(0)
    , mySharedData(0)
    , myIdx(0)
    , mySource(MEMVIEW_SHM)
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
    if (mySource == MEMVIEW_SHM)
    {
	int		shm_fd;

	// Set of shared memory before fork
	shm_fd = shm_open(SHARED_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
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

	// Copy stderr to the output of the pipe
	dup2(fd[1], 2);

	static const int	 theMaxArgs = 256;
	const char		*args[theMaxArgs];
	int			 vg_args = 0;

	args[vg_args++] = "valgrind";
	if (mySource != LACKEY)
	{
	    args[vg_args++] = "--tool=memview";
	    if (mySource == MEMVIEW_SHM)
		args[vg_args++] = "--shared-mem=/dev/shm"SHARED_NAME;
	}
	else
	    args[vg_args++] = "--tool=lackey";
	args[vg_args++] = "--trace-mem=yes";
	for (int i = 0; i < argc; i++)
	    args[i+vg_args] = argv[i];
	args[argc+vg_args] = NULL;

	if (execvp("valgrind", (char * const *)args) == -1)
	{
	    perror("exec failed");
	    return false;
	}
	// Unreachable
    }

    // Close output for parent
    close(fd[1]);

    // Open the pipe for reading
    myPipe = fdopen(fd[0], "r");

    // Load the initial 5 lines ("==")
    // TODO: Fragile
    if (mySource == MEMVIEW_PIPE)
	loadFromLackey(5);

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
	    case LACKEY:
		rval = loadFromLackey(10000);
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
	char		type;

	char		*saveptr = 0;
	char		*type_str;
	char		*addr_str;
	char		*size_str;
	const char	*delim = " ,\n";

	type_str = strtok_r(buf, delim, &saveptr);
	if (!type_str)
	    continue;
	type = type_str[0];

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

    if (fread(&block, sizeof(TraceBlock), 1, myPipe))
    {
	// Basic semantic checking to ensure we received valid data
	char	type = block.myType[0];
	if (type != 'I' && type != 'L' && type != 'S' && type != 'M')
	{
	    fprintf(stderr, "received invalid block\n");
	    return false;
	}

	for (int j = 0; j < block.myEntries; j++)
	{
	    myState->updateAddress(
		    block.myAddr[j], block.mySize[j], block.myType[j]);
	}

	return true;
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

    // Basic semantic checking to ensure we received valid data
    char	type = block.myType[0];
    if (type != 'I' && type != 'L' && type != 'S' && type != 'M')
    {
	fprintf(stderr, "received invalid block\n");
	return false;
    }

    for (int i = 0; i < block.myEntries; i++)
    {
	myState->updateAddress(
		block.myAddr[i], block.mySize[i], block.myType[i]);
    }

    block.myWSem = 1;
    myIdx++;
    if (myIdx == theBlockCount)
	myIdx = 0;

    return true;
}


