#include "DisplayLayout.h"
#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "GLImage.h"
#include <assert.h>

DisplayLayout::DisplayLayout()
    : myVisualization(HILBERT)
{
}

DisplayLayout::~DisplayLayout()
{
}

static const int	theBlockSpacing = 1;

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

static void
placeBlock(int &c, int &r, int bwidth, int bheight,
	int &maxheight, int iwidth)
{
    // Does the block fit horizontally?
    if (c + bwidth > iwidth)
    {
	r += maxheight + theBlockSpacing;
	c = 0;
	maxheight = bheight;
    }
    else
    {
	maxheight = SYSmax(maxheight, bheight);
    }
}

void
DisplayLayout::update(MemoryState &state, int width)
{
    //StopWatch	timer;
    myBlocks.clear();

    MemoryState::DisplayIterator it(state);
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	auto page(it.page());
	if (!myBlocks.size())
	    myBlocks.push_back(DisplayBlock(page.addr(), page.size()));
	else
	{
	    uint64 vacant = page.addr() -
		(myBlocks.back().myAddr + myBlocks.back().mySize);
	    if (vacant >= (myBlocks.back().mySize >> 3))
		myBlocks.push_back(DisplayBlock(page.addr(), page.size()));
	    else
		myBlocks.back().mySize += page.size() + vacant;
	}
    }

    // Initialize global offsets
    if (myVisualization == LINEAR)
    {
	int r = 0;
	for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	{
	    int c = it->myAddr % width;
	    int nr = 1 + (c + it->mySize - 1) / width;

	    it->myBox.initBounds(0, r, width, r+nr);
	    it->myStartCol = c;

	    r += nr + theBlockSpacing;
	}
	myWidth = width;
	myHeight = r;
    }
    else
    {
	int r = 0;
	int c = 0;
	int maxheight = 0;
	myWidth = 0;
	for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	{
	    BlockSizer  sizer;
	    blockTraverse(0, 0, 0, sizer, it->mySize, 15,
		    myVisualization == HILBERT, 0, false);

	    int nc = sizer.myWidth;
	    int nr = sizer.myHeight;

	    placeBlock(c, r, nc, nr, maxheight, width);

	    it->myBox.initBounds(c, r, c+nc, r+nr);

	    c += nc + theBlockSpacing;
	    myWidth = SYSmax(myWidth, c);
	}
	myHeight = r + maxheight + theBlockSpacing;
    }
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

static const int theLUTLevels = 5;
static const int theLUTWidth = 1 << theLUTLevels;
static const int theLUTMask = theLUTWidth - 1;
static const int theLUTSize = 1 << (2*theLUTLevels);

class BlockFill : public Traverser {
public:
    BlockFill(int *data, int *idata)
	: myData(data), myIData(idata) {}

    virtual bool visit(int idx, int r, int c, int level, bool, int, bool)
    {
	if (level == 0)
	{
	    int rc = (r << theLUTLevels) | c;
	    myData[idx] = rc;
	    myIData[rc] = idx;
	}
	return true;
    }

public:
    int	    *myData;
    int	    *myIData;
};

// This is only valid for idx in the range 0 to 1023
class BlockLUT {
public:
    BlockLUT()
    {
	for (int i = 0; i < theLUTSize; i++)
	{
	    int	r, c;
	    getBlockCoord(r, c, i);
	    int rc = (r << theLUTLevels) | c;
	    myBlock[i] = rc;
	    myIBlock[rc] = i;
	}
	for (int level = 0; level <= theLUTLevels; level++)
	{
	    for (int r = 0; r < 4; r++)
	    {
		for (int f = 0; f < 2; f++)
		{
		    BlockFill   fill(
			    myHilbert[level][r][f],
			    myIHilbert[level][r][f]);
		    blockTraverse(0, 0, 0, fill, theLUTSize, level, true, r, f);
		}
	    }
	}
    }

    void smallBlock(int &r, int &c, int idx)
    {
	c = myBlock[idx];
	r = c >> theLUTLevels;
	c &= theLUTMask;
    }
    void smallHilbert(int &r, int &c, int idx, int level, int rotate, bool flip)
    {
	c = myHilbert[level][rotate][flip][idx];
	r = c >> theLUTLevels;
	c &= theLUTMask;
    }

    const int *getIBlock()
    {
	return myIBlock;
    }
    const int *getIHilbert(int level, int rotate, bool flip)
    {
	return myIHilbert[level][rotate][flip];
    }

private:
    int		myBlock[theLUTSize];
    int		myIBlock[theLUTSize];

    int		myHilbert[theLUTLevels+1][4][2][theLUTSize];
    int		myIHilbert[theLUTLevels+1][4][2][theLUTSize];
};

static BlockLUT		theBlockLUT;

template <typename T>
static inline void
setPixel(GLImage<T> &image, int c, int r,
	const MemoryState::DisplayPage &page, uint64 off)
{
    image.setPixel(c, r, page.state(off).uval);
}

template <>
inline void
setPixel<uint64>(GLImage<uint64> &image, int c, int r,
	const MemoryState::DisplayPage &page, uint64 off)
{
    image.setPixel(c, r, page.addr() + off);
}

template <typename T>
class PlotImage : public Traverser {
public:
    PlotImage(MemoryState &state,
	    GLImage<T> &image, uint64 addr, int roff, int coff)
	: myState(state)
	, myImage(image)
	, myAddr(addr)
	, myRowOff(roff)
	, myColOff(coff)
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

	if (level <= theLUTLevels)
	{
	    uint64 off;
	    auto page = myState.getPage(myAddr + idx, off);

	    if (!page.exists())
		return false;

	    int	size = bsize*bsize;
	    assert(off + size <= page.size());

	    page.resetDirty();

	    const int *lut = hilbert ?
		theBlockLUT.getIHilbert(level, rotate, flip) :
		theBlockLUT.getIBlock();

	    for (int r = 0, rc = 0; r < bsize; r++)
	    {
		for (int c = 0; c < bsize; c++, rc++)
		    setPixel<T>(myImage, c+coff, r+roff, page, off+lut[rc]);
		// The LUT might have been created for a different size
		// block
		rc += theLUTWidth - bsize;
	    }

	    return false;
	}

	return true;
    }

private:
    MemoryState	&myState;
    GLImage<T> &myImage;
    uint64   myAddr;
    int	     myRowOff;
    int	     myColOff;
};

template <typename T>
void
DisplayLayout::fillImage(
	GLImage<T> &image,
	MemoryState &state,
	int coff, int roff) const
{
    image.fill(0);

    for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
    {
	Box	ibox;
	
	ibox.initBounds(coff, roff, coff+image.width(), roff+image.height());

	if (!ibox.intersect(it->myBox))
	    continue;

	if (myVisualization == LINEAR)
	{
	    uint64	addr = it->myAddr;
	    int		c = it->myStartCol;

	    if (ibox.ymin() > it->myBox.ymin())
	    {
		addr += (ibox.ymin() - it->myBox.ymin())*it->myBox.width();
		addr -= it->myStartCol;
		c = it->myBox.xmin();
	    }
	    if (ibox.xmin() > c)
	    {
		addr += ibox.xmin() - c;
		c = ibox.xmin();
	    }

	    for (int r = ibox.ymin(); r < ibox.ymax(); r++)
	    {
		for (; c < ibox.xmax() && addr < it->end(); c++, addr++)
		{
		    uint64  off;
		    auto    page = state.getPage(addr, off);
		    setPixel<T>(image, c-coff, r-roff, page, off);
		}
		addr += it->myBox.width() - ibox.width();
		c = ibox.xmin();
	    }
	}
	else
	{
	    PlotImage<T> plot(state, image, it->myAddr,
		    it->myBox.ymin()-roff,
		    it->myBox.xmin()-coff);

	    blockTraverse(0, 0, 0, plot, it->mySize, 15,
		    myVisualization == HILBERT, 0, false);
	}
    }
}

#define INST_FUNC(TYPE) template void DisplayLayout::fillImage<TYPE>( \
	GLImage<TYPE> &image, MemoryState &state, int coff, int roff) const;

INST_FUNC(uint32)
INST_FUNC(uint64)

uint64
DisplayLayout::queryPixelAddress(
	MemoryState &state,
	int coff, int roff) const
{
    GLImage<uint64> image;

    // Fill a 1x1 image with the memory address for the query pixel

    image.resize(1, 1);

    fillImage(image, state, coff, roff);
    return *image.data();
}

