#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "Loader.h"
#include "GLImage.h"
#include <assert.h>

MemoryState::MemoryState()
    : myTime(1)
    , myHRTime(1)
    , myLoader(0)
    , myIgnoreBits(2)
    , myVisualization(HILBERT)
{
    memset(myTable, 0, theTopSize*sizeof(State *));
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

static const int	theBlockSpacing = 1;

void
MemoryState::fillLinear(GLImage<uint32> &image, AnchorInfo &info) const
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
		if (r == info.myQuery.y())
		{
		    int	off = i + info.myQuery.x();
		    if (off < it.size())
		    {
			info.myQueryAddr = it.addr() + off;
			info.myQueryAddr <<= myIgnoreBits;
		    }
		}

		for (; c < image.width() && i < it.size(); c++, i++)
		{
		    int	     tidx = (it.addr() + i)>>theBottomBits;
		    int	     bidx = (it.addr() + i)&theBottomMask;
		    StateArray  *arr = myTable[tidx];

		    if (arr->myState[bidx])
			image.setPixel(r, c, mapColor(arr->myState[bidx],
						      arr->myType[bidx]));
		}
		c = 0;
	    }
	}

	r = nr;
	r += theBlockSpacing+1;
    }

    info.myAbsoluteOffset = info.myAnchorOffset;
    info.myHeight = r + info.myAnchorOffset;
    info.myWidth = 0;
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

// A callback for recursive block traversal
class Traverser {
public:
    // Return false if you don't want any further traversal
    virtual bool	visit(int idx, int r, int c, int level,
			      bool hilbert, int rotate, bool flip) = 0;
};

static void
blockTraverse(int idx, int roff, int coff,
	Traverser &traverser, int size, int level,
	bool hilbert, int rotate, bool flip)
{
    // Only calls the traverser for full blocks
    if (size >= (1 << (2*level)))
    {
	if (!traverser.visit(idx, roff, coff, level, hilbert, rotate, flip)
		|| level == 0)
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

class BlockFill : public Traverser {
public:
    BlockFill(int *data)
	: myData(data) {}

    virtual bool visit(int idx, int r, int c, int level, bool, int, bool)
    {
	if (level == 0)
	    myData[idx] = r | (c << 16);
	return true;
    }

public:
    int	    *myData;
};

class BlockLUT {
public:
    BlockLUT()
    {
	for (int i = 0; i < 256; i++)
	{
	    int	r, c;
	    getBlockCoord(r, c, i);
	    myBlock[i] = r | (c<<16);
	}
	for (int level = 0; level <= 4; level++)
	{
	    for (int r = 0; r < 4; r++)
	    {
		for (int f = 0; f < 2; f++)
		{
		    BlockFill   fill(myHilbert[level][r][f]);
		    blockTraverse(0, 0, 0, fill, 256, level, true, r, f);
		}
	    }
	}
    }

    void lookup(int &r, int &c, int idx)
    {
	r = (myBlock[idx & 0xFF] |
	     (myBlock[(idx>>8) & 0xFF] << 4)) |
	    ((myBlock[(idx>>16) & 0xFF] << 8) |
	     (myBlock[idx>>24] << 12));
	c = r>>16;
	r &= 0xFFFF;
    }
    // This is only valid for idx in the range 0 to 255
    void smallBlock(int &r, int &c, int idx)
    {
	r = myBlock[idx];
	c = r>>16;
	r &= 0xFFFF;
    }
    void smallHilbert(int &r, int &c, int idx, int level, int rotate, bool flip)
    {
	r = myHilbert[level][rotate][flip][idx];
	c = r>>16;
	r &= 0xFFFF;
    }

private:
    int		myBlock[256];
    int		myHilbert[5][4][2][256];
};

static BlockLUT		theBlockLUT;

class BlockSizer : public Traverser {
public:
    BlockSizer()
	: myWidth(0)
	, myHeight(0) {}

    void reset() { myWidth = myHeight = 0; }
    virtual bool visit(int, int r, int c, int level, bool, int, bool)
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
	    GLImage<uint32> &image, uint64 addr, int roff, int coff,
	    const QPoint &query)
	: myState(state)
	, myImage(image)
	, myAddr(addr)
	, myRowOff(roff)
	, myColOff(coff)
	, myQuery(query)
	, myQueryAddr(0)
	{}

    virtual bool visit(int idx, int r, int c, int level,
		       bool hilbert, int rotate, bool flip)
    {
	int bsize = 1 << level;
	int roff = myRowOff + r;
	int coff = myColOff + c;

	// Discard boxes that are outside the valid range
	if (roff + bsize <= 0 || roff >= myImage.height() ||
	    coff + bsize <= 0 || coff >= myImage.width())
	{
	    return false;
	}

	// Subdivide more for partially overlapping boxes
	if (roff < 0 || roff + bsize > myImage.height() ||
	    coff < 0 || coff + bsize > myImage.width())
	{
	    return true;
	}

	if (level <= 4)
	{
	    int	size = bsize*bsize;
	    int	tidx = (myAddr + idx)>>MemoryState::theBottomBits;
	    int	bidx = (myAddr + idx) &MemoryState::theBottomMask;
	    MemoryState::StateArray  *arr = myState.myTable[tidx];

	    assert(bidx + size <= (int)MemoryState::theBottomSize);

	    if (myQuery.y() >= roff && myQuery.y() < roff + bsize &&
		myQuery.x() >= coff && myQuery.x() < coff + bsize)
	    {
		// This is a reverse lookup that could be precomputed
		for (int i = 0; i < size; i++)
		{
		    if (hilbert)
			theBlockLUT.smallHilbert(r, c, i, level, rotate, flip);
		    else
			theBlockLUT.smallBlock(r, c, i);
		    r += roff;
		    c += coff;
		    if (r == myQuery.y() && c == myQuery.x())
		    {
			myQueryAddr = myAddr + idx + i;
			break;
		    }
		}
	    }

	    for (int i = 0; i < size; i++, bidx++)
	    {
		if (arr->myState[bidx])
		{
		    if (hilbert)
			theBlockLUT.smallHilbert(r, c, i, level, rotate, flip);
		    else
			theBlockLUT.smallBlock(r, c, i);
		    r += roff;
		    c += coff;

		    myImage.setPixel(r, c, myState.mapColor(
				arr->myState[bidx], arr->myType[bidx]));
		}
	    }
	    return false;
	}

	return true;
    }

    uint64	getQueryAddr() const	{ return myQueryAddr; }

private:
    const MemoryState	&myState;
    GLImage<uint32> &myImage;
    uint64   myAddr;
    int	     myRowOff;
    int	     myColOff;
    QPoint	myQuery;
    uint64	myQueryAddr;
};

static void
placeBlock(int &roff, int &coff, int bwidth, int bheight,
	int &maxheight, GLImage<uint32> &image)
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

typedef std::pair<uint64, int>	AddrRow;

void
MemoryState::fillRecursiveBlock(GLImage<uint32> &image, AnchorInfo &info) const
{
    int		 r = 0;
    int		 c = 0;
    int		 maxheight = 0;
    int		 maxwidth = 0;
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
	maxwidth = SYSmax(maxwidth, bwidth);

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
	    PlotImage	plot(*this, image, addr,
		    r, c - info.myColumn, info.myQuery);
	    blockTraverse(0, 0, 0, plot, size, 15,
		    myVisualization == HILBERT, 0, false);

	    // Set the query address
	    if (plot.getQueryAddr())
	    {
		info.myQueryAddr = plot.getQueryAddr();
		info.myQueryAddr <<= myIgnoreBits;
	    }
	}
	c += bwidth + theBlockSpacing;
    }

    info.myHeight = r + maxheight + info.myAbsoluteOffset;
    info.myWidth = maxwidth;
}

void
MemoryState::fillImage(GLImage<uint32> &image, AnchorInfo &info) const
{
    //StopWatch	 timer;
    image.fill(0);

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

