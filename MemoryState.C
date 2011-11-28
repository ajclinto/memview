#include "MemoryState.h"

MemoryState::MemoryState()
    : myTime(1)
    , myChild(-1)
    , myPipe(0)
{
    memset(myTable, 0, theTopSize*sizeof(State *));
}

MemoryState::~MemoryState()
{
    // Lackey doesn't seem to have a SIGINT signal handler, so send it the
    // kill signal.
    if (myChild > 0)
	kill(myChild, SIGKILL);

    if (myPipe)
	fclose(myPipe);

    for (uint32 i = 0; i < theTopSize; i++)
	if (myTable[i])
	    delete [] myTable[i];
}

bool
MemoryState::openPipe(const char *command)
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

	execlp("valgrind", "valgrind",
		"--tool=lackey", "--trace-mem=yes",
		command, "10000", "100", NULL);
	// Unreachable
    }

    // Close output for parent
    close(fd[1]);

    // Open the pipe for reading
    myPipe = fdopen(fd[0], "r");

    return true;
}

bool
MemoryState::loadFromPipe(int max_read)
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

	addr >>= theIgnoreBits;
	addr &= 0xFFFFFFFF;
	size >>= theIgnoreBits;
	size = SYSmax(size, 1);
	for (int i = 0; i < size; i++)
	    setEntry((uint32)addr+i, myTime);
	myTime++;
    }

    if (buf)
	free(buf);

    return i > 0;
}

static const uint32	theWhite = 0xFFFFFFFF;
static const uint32	theBlack = 0xFF000000;

static inline bool
putNextPixel(QRgb *&data, int &r, int &c, QImage &image, uint32 val)
{
    data[c++] = val;
    if (c >= image.width())
    {
	r++;
	if (r >= image.height())
	    return false;
	data = (QRgb *)image.scanLine(r);
	c = 0;
    }
    return true;
}

void
MemoryState::fillImage(QImage &image) const
{
    int		 r = 0;
    int		 c = 0;
    int		 empty_count = 0;
    QRgb	*data = (QRgb *)image.scanLine(r);

    image.fill(theBlack);

    // Assume that the stack occupies the top half of memory
    for (int i = 0; i < theTopSize; i++)
    {
	if (myTable[i])
	{
	    for (uint32 j = 0; j < theBottomSize; j++)
	    {
		if (myTable[i][j])
		{
		    if (empty_count >= image.width())
		    {
			// Put a blank line
			r++;
			c = 0;
			if (r >= image.height())
			    return;
		    }
		    empty_count = 0;
		    if (!putNextPixel(data, r, c, image,
				mapColor(myTable[i][j])))
			return;
		}
		else if (empty_count < image.width())
		{
		    if (!putNextPixel(data, r, c, image, theBlack))
			return;
		    empty_count++;
		}
	    }
	}
    }
}

