#include "Loader.h"
#include "MemoryState.h"

Loader::Loader(MemoryState *state)
    : QThread(0)
    , myState(state)
    , myChild(-1)
    , myPipe(0)
    , myBinary(true)
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
}

bool
Loader::openPipe(int argc, char *argv[])
{
    int		 fd[2];

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

	args[0] = "valgrind";
	if (myBinary)
	    args[1] = "--tool=memview";
	else
	    args[1] = "--tool=lackey";
	args[2] = "--trace-mem=yes";
	for (int i = 0; i < argc; i++)
	    args[i+3] = argv[i];
	args[argc+3] = NULL;

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
    loadFromLackey(5);

    return true;
}

void
Loader::run()
{
    while (!myAbort)
    {
	if (myBinary)
	{
	    if (!loadFromTrace())
		myAbort = true;
	}
	else
	{
	    if (!loadFromLackey(10000))
		myAbort = true;
	}
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

static const int	theBlockSize = 1024*16;

typedef struct {
    uint64	myAddr[theBlockSize];
    char        myType[theBlockSize];
    char        mySize[theBlockSize];
    int         myEntries;
} TraceBlock;

bool
Loader::loadFromTrace()
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


