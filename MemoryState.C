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
getBlockSize(int &w, int &h, int size)
{
    if (size == 0)
    {
	w = h = 0;
	return;
    }

    int	bits = 0;
    int	tmp = (size-1) >> 2;

    while (tmp)
    {
	bits++;
	tmp >>= 2;
    }

    if (size > (3 << (bits*2)))
    {
	w = (2 << bits);
	h = (2 << bits);
    }
    else if (size > (2 << (bits*2)))
    {
	getBlockSize(w, h, size-(2 << (bits*2)));
	w = (2 << bits);
	h += (1 << bits);
    }
    else if (size > (1 << (bits*2)))
    {
	getBlockSize(w, h, size-(1 << (bits*2)));
	w += (1 << bits);
	h = (1 << bits);
    }
    else
    {
	w = h = 1;
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
placeBlock(int &roff, int &coff, int &bwidth, int &bheight,
	int &maxheight, GLImage &image, int size)
{
    // Determine the width and height of the result block
    getBlockSize(bwidth, bheight, size);

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

void
MemoryState::plotBlock(int roff, int coff, int bwidth, int bheight,
	GLImage &image, uint64 addr, int size) const
{
    if (roff < image.height() && roff + bheight > 0)
    {
	// Display the block
	for (int i = 0; i < size; i++)
	{
	    int	r, c;
	    theBlockLUT.lookup(r, c, i);
	    r += roff;
	    c += coff;
	    if (r >= 0 && r < image.height() && c < image.width())
	    {
		int	     tidx = (addr + i)>>theBottomBits;
		int	     bidx = (addr + i)&theBottomMask;
		StateArray  *arr = myTable[tidx];

		if (arr->myState[bidx])
		    image.setPixel(r, c,
			    mapColor(arr->myState[bidx], arr->myType[bidx]));
	    }
	}
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
	for (int i = 0; i < it.size(); i += maxsize)
	{
	    uint64  addr = it.addr() + i;
	    int	    size = SYSmin(it.size()-i, maxsize);
	    int	    bwidth, bheight;

	    placeBlock(r, c, bwidth, bheight, maxheight, image, size);

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
			    i = 0;
			    addr = it.addr();
			    size = SYSmin(it.size(), maxsize);
			    getBlockSize(bwidth, bheight, size);
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
		// This will try to anchor the view in a block that is
		// somewhere in the middle of the screen
		if (c == 0 && r < (image.height()>>1))
		{
		    info.myAnchorAddr = addr;
		    info.myAnchorOffset = -r;
		}
		plotBlock(r, c, bwidth, bheight, image, addr, size);
	    }
	    c += bwidth + theBlockSpacing;
	}
    }

    info.myHeight = r + maxheight + info.myAbsoluteOffset;
}

void
MemoryState::QuadTree::addChild(int level, int r, int c, StateArray *arr)
{
    if (level)
    {
	int idx;

	level--;
	idx  = r&(1<<level) ? 2 : 0;
	idx |= c&(1<<level) ? 1 : 0;
	if (!mySubtree[idx])
	    mySubtree[idx] = new QuadTree;
	mySubtree[idx]->addChild(level, r, c, arr);
    }
    else
    {
	myLeaf = arr;
    }
}

QSize
MemoryState::QuadTree::computeWSize(int off)
{
    QSize	size(0, 0);
    if (mySubtree[off])
    {
	size = mySubtree[off]->computeSize();
    }
    if (mySubtree[off+1])
    {
	QSize	tmp(mySubtree[off+1]->computeSize());
	size = QSize(size.width() + tmp.width(),
		SYSmax(size.height(), tmp.height()));
    }
    return size;
}

QSize
MemoryState::QuadTree::computeSize()
{
    if (myLeaf)
    {
	int width = 1<<(theBottomBits>>1);
	mySize = QSize(width, width);
    }
    else
    {
	QSize	tmp;

	mySize = computeWSize(0);
	tmp = computeWSize(2);

	mySize = QSize(SYSmax(mySize.width(), tmp.width()),
		mySize.height() + tmp.height());
    }
    return mySize;
}

void
MemoryState::QuadTree::render(GLImage &image,
	const QPoint &off, const MemoryState &state) const
{
    QRect	box(off, mySize);
    QRect	ibox(0, 0, image.width(), image.height());

    // Culling
    if (!box.intersects(ibox))
	return;

    if (myLeaf)
    {
	// Display the block
	for (uint32 i = 0; i < theBottomSize; i++)
	{
	    if (myLeaf->myState[i])
	    {
		int	r, c;
		theBlockLUT.lookup(r, c, i);
		r += off.y();
		c += off.x();
		if (r >= 0 && r < image.height() &&
		    c >= 0 && c < image.width())
		    image.setPixel(r, c,
			    state.mapColor(myLeaf->myState[i],
					   myLeaf->myType[i]));
	    }
	}
    }
    else
    {
	QPoint	xoff = off;
	QPoint	yoff = off;
	if (mySubtree[0])
	{
	    mySubtree[0]->render(image, off, state);
	    xoff = QPoint(off.x()+mySubtree[0]->getSize().width(), off.y());
	    yoff = QPoint(off.x(), off.y()+mySubtree[0]->getSize().height());
	}
	if (mySubtree[1])
	{
	    mySubtree[1]->render(image, xoff, state);
	    yoff = QPoint(off.x(), SYSmax(yoff.y(),
			off.y()+mySubtree[1]->getSize().height()));
	}
	if (mySubtree[2])
	{
	    mySubtree[2]->render(image, yoff, state);
	    yoff = QPoint(yoff.x()+mySubtree[2]->getSize().width(), yoff.y());
	}
	if (mySubtree[3])
	    mySubtree[3]->render(image, yoff, state);
    }
}

#if 0
void
MemoryState::fillRecursiveBlock(GLImage &image, const QPoint &off) const
{
    QuadTree	tree;

    // Build the quad-tree
    for (uint32 i = 0; i < theTopSize; i++)
    {
	if (myTable[i])
	{
	    int	r, c;
	    theBlockLUT.lookup(r, c, i);
	    tree.addChild(theTopBits>>1, r, c, myTable[i]);
	}
    }
    tree.computeSize();
    tree.render(image, off, *this);
}
#endif

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
	    fillRecursiveBlock(image, info);
	    break;
    }
}

