#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "Loader.h"
#include "GLImage.h"
#include <assert.h>

static void
fillLut(uint32 *lut, const Color &hi, const Color &lo)
{
    const uint32	lcutoff = 120;
    const uint32	hcutoff = 230;
    Color		vals[4];

    vals[0] = lo * (0.02/ lo.luminance());
    vals[1] = lo * (0.15 / lo.luminance());
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
    , myHRTime(1)
    , myLoader(0)
    , myIgnoreBits(2)
    , myVisualization(BLOCK)
{
    memset(myTable, 0, theTopSize*sizeof(State *));

    Color	rhi(0.2, 1.0, 0.2);
    Color	whi(1.0, 0.7, 0.2);
    Color	ihi(0.3, 0.2, 0.8);

    Color	rlo(0.1, 0.1, 0.5);
    Color	wlo(0.3, 0.1, 0.1);
    Color	ilo(0.3, 0.1, 0.4);

    fillLut(myILut, ihi, ilo);
    fillLut(myWLut, whi, wlo);
    fillLut(myRLut, rhi, rlo);
}

MemoryState::~MemoryState()
{
    delete myLoader;

    for (uint64 i = 0; i < theTopSize; i++)
	if (myTable[i])
	    delete [] myTable[i];
}

bool
MemoryState::openPipe(int argc, char *argv[])
{
    const char	*ignore = extractOption(argc, argv, "--ignore-bits=");

    if (ignore)
	myIgnoreBits = atoi(ignore);

    myLoader = new Loader(this);

    if (myLoader->openPipe(argc, argv))
    {
	// Start loading data in a new thread
	myLoader->start();
	return true;
    }

    return false;
}

void
MemoryState::incrementTime()
{
    if (sizeof(State) == sizeof(uint32))
    {
	myTime++;
    }
    else
    {
	myHRTime++;
	if ((myHRTime & ((1 << 8*(sizeof(uint32)-sizeof(State)))-1)) == 0)
	    myTime++;
    }

    // The time wrapped
    if (myTime == theFullLife || myTime == theHalfLife)
    {
	StateIterator	it(this);
	for (it.rewind(); !it.atEnd(); it.advance())
	{
	    if ((myTime == theFullLife && it.state() < theHalfLife) ||
		(myTime == theHalfLife && it.state() >= theHalfLife &&
		 it.state() <= theFullLife))
		it.setState(theStale);
	}
	if (myTime == theFullLife)
	    myTime = 1;
	else
	    myTime++;
    }
}

static const uint32	theWhite = 0xFFFFFFFF;
static const uint32	theBlack = 0xFF000000;

static const int	theBlockSpacing = 1;

void
MemoryState::fillLinear(GLImage &image, AnchorInfo &info) const
{
    int		 r = -info.myAnchorOffset;
    int		 c = 0;

    DisplayIterator it(this);
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	// Calculate a consistent column offset
	c = it.addr() % image.width();
	int nr = r + (c + it.size() - 1) / image.width();
	if (nr >= 0 && r < image.height())
	{
	    int	i = 0;
	    if (r < 0)
	    {
		i = -r*image.width() - c;
		r = 0;
		c = 0;
	    }
	    for (; r <= SYSmin(nr, image.height()-1) &&
		    i < it.size(); r++)
	    {
		for (; c < image.width() && i < it.size(); c++, i++)
		{
		    int	     tidx = (it.addr() + i)>>theBottomBits;
		    int	     bidx = (it.addr() + i)&theBottomMask;
		    StateArray  *arr = myTable[tidx];

		    if (arr->myState[bidx])
			image.setPixel(r, c,
				mapColor(arr->myState[bidx], arr->myType[bidx]));
		}
		c = 0;
	    }
	}

	r = nr;
	r += theBlockSpacing+1;
    }

    info.myAbsoluteOffset = info.myAnchorOffset;
    info.myHeight = r + info.myAnchorOffset;
}

static void
getBlockCoord(int &r, int &c, int idx)
{
    int	bit = 0;

    r = c = 0;
    while (idx)
    {
	c |= (idx & 1) ? (1 << bit) : 0;
	r |= (idx & 2) ? (1 << bit) : 0;
	idx >>= 2;
	bit++;
    }
}

class BlockLUT {
public:
    BlockLUT()
    {
	for (int i = 0; i < 256; i++)
	{
	    int	r, c;
	    getBlockCoord(r, c, i);
	    myRowCol[i] = r | (c<<16);
	}
    }

    void	lookup(int &r, int &c, int idx)
    {
	r = (myRowCol[idx & 0xFF] |
	     (myRowCol[(idx>>8) & 0xFF] << 4)) |
	    ((myRowCol[(idx>>16) & 0xFF] << 8) |
	     (myRowCol[idx>>24] << 12));
	c = (r>>16) & 0xFFFF;
	r &= 0xFFFF;
    }

private:
    int		myRowCol[256];
};

static BlockLUT		theBlockLUT;

static void
placeBlock(int &roff, int &coff, int bwidth, int bheight,
	int &maxheight, GLImage &image)
{
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
}

// A callback for recursive block traversal
class Traverser {
public:
    // Return false if you don't want any further traversal
    virtual bool	visit(int idx, int r, int c, int level) = 0;
};

class BlockSizer : public Traverser {
public:
    BlockSizer()
	: myWidth(0)
	, myHeight(0) {}

    void reset() { myWidth = myHeight = 0; }
    virtual bool visit(int, int r, int c, int level)
    {
	int bsize = 1 << level;
	myHeight = SYSmax(myHeight, r + bsize);
	myWidth = SYSmax(myWidth, c + bsize);
	return false;
    }

public:
    int	     myWidth;
    int	     myHeight;
};

class PlotImage : public Traverser {
public:
    PlotImage(const MemoryState &state,
	    GLImage &image, uint64 addr, int roff, int coff)
	: myState(state)
	, myImage(image)
	, myAddr(addr)
	, myRowOff(roff)
	, myColOff(coff) {}

    virtual bool visit(int idx, int r, int c, int level)
    {
	int bsize = 1 << level;
	int roff = myRowOff + r;
	int coff = myColOff + c;

	if (roff + bsize <= 0 || roff >= myImage.height() ||
	    coff + bsize <= 0 || coff >= myImage.width())
	{
	    return false;
	}

	if (level == 0)
	{
	    int		 tidx = (myAddr + idx)>>MemoryState::theBottomBits;
	    int		 bidx = (myAddr + idx) &MemoryState::theBottomMask;
	    MemoryState::StateArray  *arr = myState.myTable[tidx];

	    if (arr->myState[bidx])
		myImage.setPixel(roff, coff, myState.mapColor(
			    arr->myState[bidx], arr->myType[bidx]));
	    return false;
	}

	return true;
    }

private:
    const MemoryState	&myState;
    GLImage &myImage;
    uint64   myAddr;
    int	     myRowOff;
    int	     myColOff;
};

static void
blockTraverse(int idx, int roff, int coff,
	Traverser &traverser, int size, int level,
	bool hilbert, int rotate, bool flip)
{
    // Only calls the traverser for full blocks
    if (size >= (1 << (2*level)))
    {
	if (!traverser.visit(idx, roff, coff, level) || level == 0)
	    return;
    }

    int	s = 1 << (level-1);
    int	off = s*s;
    int	rs[4], cs[4];
    int	map[4];

    if (hilbert)
    {
	for (int i = 0; i < 4; i++)
	    map[i] = (rotate + i) & 3;
	if (flip)
	    SYSswap(map[1], map[3]);
    }
    else
    {
	map[0] = 0;
	map[1] = 2;
	map[2] = 3;
	map[3] = 1;
    }

    rs[map[0]] = 0; cs[map[0]] = 0;
    rs[map[1]] = s; cs[map[1]] = 0;
    rs[map[2]] = s; cs[map[2]] = s;
    rs[map[3]] = 0; cs[map[3]] = s;

    blockTraverse(idx, roff + rs[0], coff + cs[0],
	    traverser, SYSmin(size, off), level-1,
	    hilbert, rotate, !flip);
    if (size > off)
    {
	blockTraverse(idx+off, roff + rs[1], coff + cs[1],
	    traverser, SYSmin(size-off, off), level-1,
	    hilbert, rotate, flip);
    }
    if (size > 2*off)
    {
	blockTraverse(idx+2*off, roff + rs[2], coff + cs[2],
	    traverser, SYSmin(size-2*off, off), level-1,
	    hilbert, rotate, flip);
    }
    if (size > 3*off)
    {
	blockTraverse(idx+3*off, roff + rs[3], coff + cs[3],
	    traverser, SYSmin(size-3*off, off), level-1,
	    hilbert, rotate ^ 2, !flip);
    }
}

typedef std::pair<uint64, int>	AddrRow;

void
MemoryState::fillRecursiveBlock(GLImage &image, AnchorInfo &info) const
{
    int		 r = 0;
    int		 c = 0;
    int		 maxheight = 0;
    int		 maxsize = 1;

    // Find the greatest power of 2 less than the image size, so that we
    // can clamp the block size to this amount.
    while (maxsize < SYSmin(image.width(), image.height()))
	maxsize <<= 1;
    maxsize >>= 1;
    maxsize *= maxsize;

    // Whenever we start a new row, keep track of the address/row mapping
    // so that it's possible to search backward for an anchor point if
    // needed.
    std::vector<AddrRow>	rows;
    bool			found = false;

    DisplayIterator it(this);
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	uint64	    addr = it.addr();
	int	    size = it.size();
	int	    bwidth, bheight;

	BlockSizer  sizer;
	blockTraverse(0, 0, 0, sizer, size, 15,
		myVisualization == HILBERT, 0, false);
	bwidth = sizer.myWidth;
	bheight = sizer.myHeight;

	placeBlock(r, c, bwidth, bheight, maxheight, image);

	if (!found)
	{
	    if (c == 0)
		rows.push_back(AddrRow(addr, r));

	    if (addr >= info.myAnchorAddr)
	    {
		// Search back until we find the first row that needs
		// to be plotted
		int	j;
		for (j = rows.size(); j-- > 0; )
		{
		    if (rows[j].second <= r + info.myAnchorOffset)
		    {
			// Also rewind addr, size, etc.
			it.rewind(rows[j].first);
			addr = it.addr();
			size = it.size();

			sizer.reset();
			blockTraverse(0, 0, 0, sizer, size, 15,
				myVisualization == HILBERT, 0, false);
			bwidth = sizer.myWidth;
			bheight = sizer.myHeight;

			maxheight = bheight;

			info.myAbsoluteOffset = r + info.myAnchorOffset;
			r = rows[j].second - info.myAbsoluteOffset;
			break;
		    }
		}
		if (j < 0)
		{
		    it.rewind();
		    info.myAbsoluteOffset = 0;
		    r = 0;
		}

		c = 0;

		found = true;
	    }
	}

	if (found)
	{
	    if (c == 0 && r <= 0)
	    {
		info.myAnchorAddr = addr;
		info.myAnchorOffset = -r;
	    }
	    PlotImage	plot(*this, image, addr, r, c);
	    blockTraverse(0, 0, 0, plot, size, 15,
		    myVisualization == HILBERT, 0, false);
	}
	c += bwidth + theBlockSpacing;
    }

    info.myHeight = r + maxheight + info.myAbsoluteOffset;
}

void
MemoryState::fillImage(GLImage &image, AnchorInfo &info) const
{
    //StopWatch	 timer;
    image.fill(theBlack);

    switch (myVisualization)
    {
	case LINEAR:
	    fillLinear(image, info);
	    break;
	case BLOCK:
	case HILBERT:
	    fillRecursiveBlock(image, info);
	    break;
    }
}

