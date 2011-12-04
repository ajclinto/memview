#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include <assert.h>

static void
fillLut(uint32 *lut, const Color &hi, const Color &lo)
{
    const uint32	lcutoff = 100;
    const uint32	hcutoff = 140;
    Color		vals[4];

    vals[0] = Color(0,0,0);
    vals[1] = lo * (0.2 / lo.luminance());
    vals[2] = hi * (0.5 / hi.luminance());
    vals[3] = hi * (2.0 / hi.luminance());

    for (uint32 i = 0; i < 256; i++)
    {
	Color	val;
	if (i >= hcutoff)
	    val = vals[2].lerp(vals[3], (i-hcutoff)/(float)(255-hcutoff));
	else if (i >= lcutoff)
	    val = vals[1].lerp(vals[2], (i-lcutoff)/(float)(hcutoff-lcutoff));
	else
	    val = vals[0].lerp(vals[1], i/(float)lcutoff);
	lut[i] = val.toInt32();
    }
}

MemoryState::MemoryState()
    : myTime(1)
    , myChild(-1)
    , myPipe(0)
    , myIgnoreBits(2)
    , myVisualization(BLOCK)
{
    memset(myTable, 0, theTopSize*sizeof(State *));

    Color	rhi(0.2, 1.0, 0.2);
    Color	whi(0.2, 0.5, 1.0);
    Color	ihi(0.5, 1.0, 0.2);

    Color	rlo(0.1, 0.1, 0.5);
    Color	wlo(0.3, 0.1, 0.1);
    Color	ilo(0.3, 0.1, 0.4);

    fillLut(myILut, ihi, ilo);
    fillLut(myWLut, whi, wlo);
    fillLut(myRLut, rhi, rlo);
}

MemoryState::~MemoryState()
{
    // Lackey doesn't seem to have a SIGINT signal handler, so send it the
    // kill signal.
    if (myChild > 0)
	kill(myChild, SIGKILL);

    if (myPipe)
	fclose(myPipe);

    for (uint64 i = 0; i < theTopSize; i++)
	if (myTable[i])
	    delete [] myTable[i];
}

bool
MemoryState::openPipe(int argc, char *argv[])
{
    static const char	*theIgnoreOption = "--ignore-bits=";
    int			 fd[2];

    for (int i = 0; i < argc; i++)
    {
	if (!strncmp(argv[i], theIgnoreOption, strlen(theIgnoreOption)))
	{
	    argv[i] += strlen(theIgnoreOption);
	    myIgnoreBits = atoi(argv[i]);
	    argc--; argv++;
	    break;
	}
    }

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

	addr >>= myIgnoreBits;
	if (addr & ~theAllMask)
	    fprintf(stderr, "clipping address %llx\n", addr);
	addr &= theAllMask;
	size >>= myIgnoreBits;
	size = SYSmax(size, 1);
	for (int i = 0; i < size; i++)
	    setEntry(addr+i, myTime, type);
	myTime++;

	// The time wrapped
	if (myTime == 0)
	    myTime = 1;
    }

    if (buf)
	free(buf);

    return i > 0;
}

static const uint32	theWhite = 0xFFFFFFFF;
static const uint32	theBlack = 0xFF000000;

static inline void
putPixel(int r, int c, QImage &image, uint32 val)
{
    QRgb	*data = (QRgb *)image.scanLine(r);
    data[c] = val;
}

static inline bool
putNextPixel(int &r, int &c, QImage &image, uint32 val)
{
    putPixel(r, c, image, val);
    c++;
    if (c >= image.width())
    {
	r++;
	if (r >= image.height())
	    return false;
	c = 0;
    }
    return true;
}

static const int	theBlockSpacing = 0;

void
MemoryState::fillLinear(QImage &image) const
{
    //StopWatch	 timer;
    int		 r = 0;
    int		 c = 0;

    // Assume that the stack occupies the top half of memory
    StateIterator	it(this);
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	if (it.nempty() >= (uint64)image.width())
	{
	    // Put a blank line
	    r += theBlockSpacing+1;
	    c = 0;
	}
	else
	{
	    // Skip the empty pixels
	    c += it.nempty();
	    if (c >= image.width())
	    {
		c -= image.width();
		r++;
	    }
	}
	if (r >= image.height())
	    return;

	if (!putNextPixel(r, c, image, mapColor(it.state(), it.type())))
	    return;
    }
}

static void
getBlockSize(int &w, int &h, int size)
{
    int	bits = 0;
    int	tmp = size >> 2;

    while (tmp)
    {
	bits++;
	tmp >>= 2;
    }

    if (size >= (3 << (bits*2)))
    {
	w = (2 << bits);
	h = (2 << bits);
    }
    else if (size >= (2 << (bits*2)))
    {
	getBlockSize(w, h, size-(2 << (bits*2)));
	w = (2 << bits);
	h += (1 << bits);
    }
    else if (size >= (1 << (bits*2)))
    {
	getBlockSize(w, h, size-(1 << (bits*2)));
	w += (1 << bits);
	h = (1 << bits);
    }
    else
    {
	w = h = 0;
    }
}

static void
getBlockCoord(int &r, int &c, int idx)
{
    int	bit = 0;

    r = c = 0;
    while (idx)
    {
	int	tmp = idx & 0x3;
	if (tmp & 1)
	    c += (1 << bit);
	if (tmp & 2)
	    r += (1 << bit);
	idx >>= 2;
	bit++;
    }
}

static const int	theMinBlockWidth = 16;
static const int	theMinBlockSize = theMinBlockWidth*theMinBlockWidth;

static bool
plotBlock(int &roff, int &coff, int &maxheight,
	QImage &image, const std::vector<uint32> &data)
{
    // Determine the width and height of the result block
    int	bwidth, bheight;

    getBlockSize(bwidth, bheight, data.size());

    // Force a minimum size
    bwidth = SYSmax(bwidth, theMinBlockWidth);
    bheight = SYSmax(bheight, theMinBlockWidth);

    // Does the block fit horizontally?
    if (coff + bwidth > image.width())
    {
	roff += maxheight + theBlockSpacing;
	coff = 0;
	maxheight = bheight;
    }
    else
    {
	maxheight = SYSmax(maxheight, bheight);
    }

    if (roff > image.height())
	return false;

    // Display the block
    for (int i = 0; i < (int)data.size(); i++)
    {
	int	r, c;
	getBlockCoord(r, c, i);
	r += roff;
	c += coff;
	if (r < image.height() && c < image.width())
	    putPixel(r, c, image, data[i]);
    }

    coff += bwidth + theBlockSpacing;

    return true;
}

void
MemoryState::fillRecursiveBlock(QImage &image) const
{
    //StopWatch	 timer;
    int		 r = 0;
    int		 c = 0;
    int		 maxheight = 0;
    std::vector<uint32>	pending;

    // Assume that the stack occupies the top half of memory
    StateIterator	it(this);
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	if (it.nempty() >= (uint64)theMinBlockSize)
	{
	    // Plot the pending block
	    if (!plotBlock(r, c, maxheight, image, pending))
		return;

	    // Reset
	    pending.resize(0);
	}
	else
	{
	    pending.insert(pending.end(), it.nempty(), theBlack);
	}

	pending.push_back(mapColor(it.state(), it.type()));
    }

    if (pending.size())
	plotBlock(r, c, maxheight, image, pending);
}

void
MemoryState::fillImage(QImage &image) const
{
    image.fill(theBlack);

    switch (myVisualization)
    {
	case LINEAR:
	    fillLinear(image);
	    break;
	case BLOCK:
	    fillRecursiveBlock(image);
	    break;
    }
}

