/*
   This file is part of memview, a real-time memory trace visualization
   application.

   Copyright (C) 2013 Andrew Clinton

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "DisplayLayout.h"
#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "GLImage.h"
#include <assert.h>

DisplayLayout::DisplayLayout()
    : myVisualization(HILBERT)
    , myCompact(true)
{
}

DisplayLayout::~DisplayLayout()
{
}

// A callback for recursive block traversal
class Traverser {
public:
    // Return false if you don't want any further traversal
    virtual bool	visit(uint64 idx, int r, int c, int level,
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
    uint64 off = 1ull << 2*(level-1);

    int	rs[4], cs[4];
    int	map[4];

    // Switch over to recursive block for 4x4 and smaller tiles even in
    // hilbert mode.  The hilbert pattern is a little difficult to follow
    // for small blocks.
    if (hilbert && level > 2)
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

    virtual bool visit(uint64, int r, int c, int level, bool, int, bool)
    {
	int bsize = 1 << level;
	myBox.enlargeBounds(c, r, c+bsize, r+bsize);
	return false;
    }

public:
    Box<int64>	myBox;
};

static void
adjustZoom(int64 &val, int zoom)
{
    int a = (1 << zoom) - 1;
    val = (val + a) >> zoom;
}

void
DisplayLayout::update(
	MemoryState &state,
	int64 width, int zoom)
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
	myStartLevel = 31 - (state.getIgnoreBits() >> 1);
	myWidth = 0;
	myHeight = 0;
	for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	{
	    BlockSizer  sizer;
	    blockTraverse(it->myAddr, it->mySize, 0, 0, sizer,
		    myStartLevel,
		    myVisualization == HILBERT, 0, false);

	    it->myBox = sizer.myBox;

	    if (!myCompact)
	    {
		it->myDisplayBox = it->myBox;

		myWidth = SYSmax(myWidth, it->myDisplayBox.xmax());
		myHeight = SYSmax(myHeight, it->myDisplayBox.ymax());
	    }
	}

	if (myCompact)
	{
	    // This method will initialize myDisplayBox for each block
	    compactBoxes<0>(myWidth);
	    compactBoxes<1>(myHeight);
	}

	if (zoom > 0)
	{
	    // Zoom grows in increments in 4x for block display.  This
	    // value will store the zoom on each axis.
	    const int zoom2 = zoom >> 1;

	    for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	    {
		// Update the address range
		int a = (1 << zoom) - 1;
		uint64 end = it->myAddr + it->mySize;
		end += a;
		end >>= zoom;
		it->myAddr >>= zoom;
		it->mySize = end - it->myAddr;

		// Update the block size
		it->myBox.l[0] >>= zoom2;
		it->myBox.l[1] >>= zoom2;
		adjustZoom(it->myBox.h[0], zoom2);
		adjustZoom(it->myBox.h[1], zoom2);

		it->myDisplayBox.l[0] >>= zoom2;
		it->myDisplayBox.l[1] >>= zoom2;
		adjustZoom(it->myDisplayBox.h[0], zoom2);
		adjustZoom(it->myDisplayBox.h[1], zoom2);
	    }

	    adjustZoom(myWidth, zoom2);
	    adjustZoom(myHeight, zoom2);

	    myStartLevel -= zoom2;
	}
    }
    else
    {
	for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	{
	    int64 r = it->myAddr / width;
	    int64 c = it->myAddr % width;
	    int64 nr = 1 + (c + it->mySize - 1) / width;

	    it->myBox.initBounds(0, r, width, r+nr);
	    it->myDisplayBox = it->myBox;
	}

	myWidth = width;
	myHeight = myBlocks.size() ? myBlocks.back().myBox.h[1] : 0;

	// Compact only in the vertical direction for linear
	if (myCompact)
	    compactBoxes<1>(myHeight);

	if (zoom > 0)
	{
	    for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
	    {
		// Update the address range
		int a = (1 << zoom) - 1;
		uint64 end = it->myAddr + it->mySize;
		end += a;
		end >>= zoom;
		it->myAddr >>= zoom;
		it->mySize = end - it->myAddr;

		// Update the block size
		it->myBox.l[1] >>= zoom;
		adjustZoom(it->myBox.h[1], zoom);

		it->myDisplayBox.l[1] >>= zoom;
		adjustZoom(it->myDisplayBox.h[1], zoom);
	    }

	    adjustZoom(myHeight, zoom);
	}
    }

}

struct Edge {
    bool operator<(const Edge &rhs) const { return myVal < rhs.myVal; }

    int64   myVal;
    int	    myIdx;
    bool    myEnd;
};

template <int dim>
void
DisplayLayout::compactBoxes(int64 &maxval)
{
    std::vector<Edge>	edges;
    for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
    {
	int idx = it - myBlocks.begin();
	edges.push_back(Edge{it->myBox.l[dim], idx, false});
	edges.push_back(Edge{it->myBox.h[dim], idx, true});
    }

    std::sort(edges.begin(), edges.end());

    int64   off = 0;
    int64   pval = 0;
    int	    in = 0;
    for (auto it = edges.begin(); it != edges.end(); ++it)
    {
	if (!in)
	    off += it->myVal - pval;

	pval = it->myVal;
	it->myVal -= off;

	if (it->myEnd)
	{
	    in--;
	    myBlocks[it->myIdx].myDisplayBox.h[dim] = it->myVal;
	}
	else
	{
	    in++;
	    myBlocks[it->myIdx].myDisplayBox.l[dim] = it->myVal;
	}
    }

    maxval = edges.size() ? edges.back().myVal : 0;
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

// This should match MemoryState::theDisplayWidthBits for maximum efficiency
static const int theLUTLevels = 6;
static const int theLUTWidth = 1 << theLUTLevels;
static const int theLUTMask = theLUTWidth - 1;
static const int theLUTSize = 1 << (2*theLUTLevels);

class BlockFill : public Traverser {
public:
    BlockFill(int *data, int *idata)
	: myData(data), myIData(idata) {}

    virtual bool visit(uint64 idx, int r, int c, int level, bool, int, bool)
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

// This is only valid for idx in the range 0 to theLUTSize-1
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

template <typename T, typename Source>
class PlotImage : public Traverser {
public:
    PlotImage(const Source &src, GLImage<T> &image, int roff, int coff)
	: mySource(src)
	, myImage(image)
	, myRowOff(roff)
	, myColOff(coff)
	{}

    virtual bool visit(uint64 idx, int r, int c, int level,
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
	    uint64 size = (uint64)bsize*(uint64)bsize;
	    uint64 off;

	    auto page = mySource.getPage(idx, size, off);

	    // This can happen when zoomed out, since the addresses no
	    // longer align perfectly with the display blocks.
	    if (off + size > page.size())
		return true;

	    if (!mySource.exists(page))
		return false;

	    const int *lut = hilbert ?
		theBlockLUT.getIHilbert(level, rotate, flip) :
		theBlockLUT.getIBlock();

	    for (int r = 0, rc = 0; r < bsize; r++)
	    {
		for (int c = 0; c < bsize; c++, rc++)
		    mySource.setPixel(myImage,
			    c+coff, r+roff, page, off+lut[rc]);
		// The LUT might have been created for a different size
		// block
		rc += theLUTWidth - bsize;
	    }

	    return false;
	}

	return true;
    }

private:
    const Source  &mySource;
    GLImage<T> &myImage;
    int	     myRowOff;
    int	     myColOff;
};

template <typename T, typename Source>
void
DisplayLayout::fillImage(
	GLImage<T> &image,
	const Source &src,
	int64 coff, int64 roff) const
{
    image.zero();

    for (auto it = myBlocks.begin(); it != myBlocks.end(); ++it)
    {
	Box<int64>	ibox;
	
	ibox.initBounds(coff, roff, coff+image.width(), roff+image.height());

	if (!ibox.intersect(it->myDisplayBox))
	    continue;

	if (myVisualization == LINEAR)
	{
	    uint64	addr = it->myAddr;
	    int64	startcol = addr % it->myDisplayBox.width();
	    int64	c = startcol;

	    if (ibox.ymin() > it->myDisplayBox.ymin())
	    {
		addr += (ibox.ymin() - it->myDisplayBox.ymin()) *
			    it->myDisplayBox.width();
		addr -= startcol;
		c = it->myDisplayBox.xmin();
	    }
	    if (ibox.xmin() > c)
	    {
		addr += ibox.xmin() - c;
		c = ibox.xmin();
	    }

	    for (int64 r = ibox.ymin(); r < ibox.ymax(); r++)
	    {
		while (c < ibox.xmax() && addr < it->end())
		{
		    uint64  off;
		    uint64  nc = SYSmin((uint64)ibox.xmax() - c,
					it->end() - addr);
		    auto    page = src.getPage(addr, nc, off);
		   
		    nc = SYSmin(nc, page.size() - off);
		    if (src.exists(page))
		    {
			src.setScanline(
				image.getScanline(r-roff) + c-coff,
				page, off, nc);
		    }

		    addr += nc;
		    c += nc;
		}
		addr += it->myDisplayBox.width() - ibox.width();
		c = ibox.xmin();
	    }
	}
	else
	{
	    int rboff = it->myBox.ymin() - it->myDisplayBox.ymin();
	    int cboff = it->myBox.xmin() - it->myDisplayBox.xmin();
	    PlotImage<T, Source> plot(src, image,
		    -(roff + rboff),
		    -(coff + cboff));

	    blockTraverse(it->myAddr, it->mySize, 0, 0, plot,
		    myStartLevel,
		    myVisualization == HILBERT, 0, false);
	}
    }
}

#define INST_FUNC(TYPE, SOURCE) \
    template void DisplayLayout::fillImage<TYPE, SOURCE>( \
	GLImage<TYPE> &image, const SOURCE &src, int64 coff, int64 roff) const;

INST_FUNC(uint32, StateSource)
INST_FUNC(uint64, AddressSource)
INST_FUNC(uint32, MMapSource)

uint64
DisplayLayout::queryPixelAddress(
	MemoryState &state,
	int64 coff, int64 roff) const
{
    GLImage<uint64> image;
    AddressSource   src(state);

    // Fill a 1x1 image with the memory address for the query pixel

    image.resize(1, 1);

    fillImage(image, src, coff, roff);
    return *image.data();
}

