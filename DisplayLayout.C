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

template <typename T>
void
setPixel(GLImage<T> &image, int c, int r,
	const MemoryState::DisplayPage &page, int off)
{
    uint32  clr = page.state(off);
    char    ty = page.type(off);

    uint32 lut = 0;
    switch (ty)
    {
	case 'l': case 'L':
	    lut = 0;
	    break;
	case 's': case 'S':
	case 'm': case 'M':
	    lut = 1;
	    break;
	case 'i': case 'I':
	    lut = 2;
	    break;
	case 'a': case 'A':
	    lut = 3;
	    break;
    }

    clr |= lut << 30;

    // Half the brightness of freed memory
    if (ty >= 'a' && ty <= 'z')
	clr |= 1 << 29;

    image.setPixel(c, r, clr);
}

template <>
void
setPixel<uint64>(GLImage<uint64> &image, int c, int r,
	const MemoryState::DisplayPage &page, int off)
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

	if (level <= 4)
	{
	    int off;
	    auto page = myState.page(myAddr + idx, off);

	    if (!page.exists())
		return false;

	    int	size = bsize*bsize;
	    assert(off + size <= page.size());

	    page.resetDirty();
	    for (int i = 0; i < size; i++)
	    {
		if (page.state(off+i))
		{
		    if (hilbert)
			theBlockLUT.smallHilbert(r, c, i, level, rotate, flip);
		    else
			theBlockLUT.smallBlock(r, c, i);
		    r += roff;
		    c += coff;

		    setPixel<T>(myImage, c, r, page, off+i);
		}
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
	    const int	width = image.width();
	    uint64	addr = it->myAddr;
	    int		c = it->myStartCol;

	    if (ibox.ymin() > it->myBox.ymin())
	    {
		addr += (width - it->myStartCol) +
		    (ibox.ymin() - it->myBox.ymin() - 1)*width;
		c = 0;
	    }

	    for (int r = ibox.ymin(); r < ibox.ymax(); r++)
	    {
		for (; c < width && addr < it->end(); c++, addr++)
		{
		    int off;
		    auto page = state.page(addr, off);
		    setPixel<T>(image, c, r-roff, page, off);
		}
		c = 0;
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
	int roff, int coff) const
{
    GLImage<uint64> image;

    // Fill a 1x1 image with the memory address for the query pixel

    image.resize(1, 1);
    image.fill(0);

    fillImage(image, state, roff, coff);
    return *image.data();
}
