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
blockTraverse(uint64 idx, uint64 size, int roff, int coff,
	Traverser &traverser, int level,
	bool hilbert, int rotate, bool flip)
{
    // Only calls the traverser for full blocks
    if (size >= (1ull << (2*level)))
    {
	if (!traverser.visit(idx, roff, coff, level, hilbert, rotate, flip)
		|| level == 0)
	    return;
    }

    int s = 1 << (level-1);
    uint64 off = s;
    off *= off;
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

    // It's assumed that idx is within the given block range.  Find the
    // relative offset within this block
    uint64 idx_rel = idx & (4*off-1);
    uint64 range[2] = {0, off};

    for (int i = 0; i < 4; i++)
    {
	uint64 start = SYSmax(idx_rel, range[0]);
	uint64 end = SYSmin(idx_rel + size, range[1]);

	if (start < range[1] && end > range[0])
	{
	    blockTraverse(
		    start + (idx - idx_rel), // Convert to an absolute index
		    end - start,	     // Convert to size
		    roff + rs[i],
		    coff + cs[i],
		    traverser,
		    level-1,
		    hilbert,
		    (i == 3) ? (rotate ^ 2) : rotate,
		    flip ^ (i == 0 || i == 3));
	}
	range[0] = range[1];
	range[1] += off;
    }
}

class BlockSizer : public Traverser {
public:
    BlockSizer()
    {
	myBox.initBounds();
    }

    virtual bool visit(int, int r, int c, int level, bool, int, bool)
    {
	int bsize = 1 << level;
	myBox.enlargeBounds(c, r, c+bsize, r+bsize);
	return false;
    }

public:
    Box	myBox;
};

static inline uint64
blockAlign(uint64 daddr, uint64 addr, uint64 size)
{
    uint64 asize = 1024;
    while (asize < (size >> 1))
	asize <<= 1;

    addr &= (asize-1);

    uint64 dalign = daddr & (asize-1);
    if (addr >= dalign)
	addr -= dalign;
    else
	addr += asize - dalign;

    return daddr + addr;
}

void
DisplayLayout::update(MemoryState &state)
{
    //StopWatch	timer;
    myBlocks.clear();

    for (MemoryState::DisplayIterator it(state); !it.atEnd(); it.advance())
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

    if (myVisualization != LINEAR)
    {
	// Initialize block display addresses
	uint64 addr = 0;
	uint64 psize = 0;

	myWidth = 0;
	myHeight = 0;
	for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	{
	    // Align the new block based on its size and the size of the
	    // previous block
	    it->myDisplayAddr =
		blockAlign(addr, it->myAddr, SYSmax(it->mySize, psize));

	    BlockSizer  sizer;
	    blockTraverse(it->myDisplayAddr, it->mySize, 0, 0, sizer,
		    15 - state.getIgnoreBits()/2,
		    myVisualization == HILBERT, 0, false);

	    it->myBox = sizer.myBox;
	    myWidth = SYSmax(myWidth, sizer.myBox.xmax());
	    myHeight = SYSmax(myHeight, sizer.myBox.ymax());

	    addr = it->myDisplayAddr + it->mySize;
	    psize = it->mySize;
	}
    }
}

static void
adjustZoom(int &val, int zoom)
{
    int a = (1 << zoom) - 1;
    val = (val + a) >> zoom;
}

void
DisplayLayout::layout(int width, int zoom)
{
    //StopWatch	timer;

    if (zoom > 0)
    {
	for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	{
	    int zoom2 = 2*zoom;
	    // Update the address range
	    int a = (1 << zoom2) - 1;
	    uint64 end = it->myAddr + it->mySize;
	    end += a;
	    end >>= zoom2;
	    it->myAddr >>= zoom2;
	    it->myDisplayAddr >>= zoom2;
	    it->mySize = end - it->myAddr;

	    // Update the block size
	    it->myBox.l[0] >>= zoom;
	    it->myBox.l[1] >>= zoom;
	    adjustZoom(it->myBox.h[0], zoom);
	    adjustZoom(it->myBox.h[1], zoom);
	}

	adjustZoom(myWidth, zoom);
	adjustZoom(myHeight, zoom);
    }

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
		    blockTraverse(0, theLUTSize, 0, 0, fill, level, true, r, f);
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
static inline void
copyScanline(T *scan,
	MemoryState::DisplayPage &page, uint64 off, int n)
{
    memcpy(scan, page.stateArray() + off, n*sizeof(T));
}

template <>
inline void
copyScanline<uint64>(uint64 *scan,
	MemoryState::DisplayPage &page, uint64 off, int n)
{
    for (int i = 0; i < n; i++)
	scan[i] = page.addr() + off + i;
}

template <typename T>
class PlotImage : public Traverser {
public:
    PlotImage(MemoryState &state,
	    GLImage<T> &image, uint64 addr, uint64 daddr, int roff, int coff)
	: myState(state)
	, myImage(image)
	, myAddr(addr)
	, myDisplayAddr(daddr)
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
	    auto page = myState.getPage(myAddr + idx - myDisplayAddr, off);

	    if (!page.exists())
		return false;

	    int	size = bsize*bsize;

	    // This can happen when zoomed out, since the addresses no
	    // longer align perfectly with the display blocks.
	    if (off + size > page.size())
		return true;

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
    uint64   myDisplayAddr;
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
    image.zero();

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
		while (c < ibox.xmax() && addr < it->end())
		{
		    uint64  off;
		    auto    page = state.getPage(addr, off);
		    int	    nc = ibox.xmax() - c;
		   
		    // It's a min with an int, so it can't be more than 32-bit
		    nc = (int)SYSmin((uint64)nc, page.size() - off);
		    nc = (int)SYSmin((uint64)nc, it->end() - addr);

		    addr += nc;
		    if (page.exists())
		    {
			copyScanline(
				image.getScanline(r-roff) + c-coff,
				page, off, nc);
		    }
		    c += nc;
		}
		addr += it->myBox.width() - ibox.width();
		c = ibox.xmin();
	    }
	}
	else
	{
	    PlotImage<T> plot(state, image,
		    it->myAddr, it->myDisplayAddr, -roff, -coff);

	    blockTraverse(it->myDisplayAddr, it->mySize, 0, 0, plot,
		    15 - state.getIgnoreBits()/2,
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

