#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "Loader.h"
#include "GLImage.h"
#include <assert.h>

static void
fillLut(uint32 *lut, const Color &hi, const Color &lo)
{
    const uint32	lcutoff = 100;
    const uint32	hcutoff = 140;
    Color		vals[4];

    vals[0] = lo * (0.04/ lo.luminance());
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
    , myHRTime(1)
    , myLoader(0)
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
    delete myLoader;

    for (uint64 i = 0; i < theTopSize; i++)
	if (myTable[i])
	    delete [] myTable[i];
}

bool
MemoryState::openPipe(int argc, char *argv[])
{
    static const char	*theIgnoreOption = "--ignore-bits=";
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
MemoryState::incrementTime(int inc)
{
    if (sizeof(State) == sizeof(uint32))
    {
	myTime += inc;
    }
    else
    {
	for (int i = 0; i < inc; i++)
	{
	    myHRTime++;
	    if ((myHRTime & ((1 << 8*(sizeof(uint32)-sizeof(State)))-1)) == 0)
		myTime++;
	}
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

static inline bool
nextPixel(int &r, int &c, const GLImage &image)
{
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

static const int	theBlockSpacing = 1;

void
MemoryState::fillLinear(GLImage &image, const QPoint &off) const
{
    int		 r = off.y();
    int		 c = 0;

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

	if (r >= 0)
	    image.setPixel(r, c, mapColor(it.state(), it.type()));

	if (!nextPixel(r, c, image))
	    return;
    }
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

static const int	theMinBlockWidth = 32;
static const int	theMinBlockSize = theMinBlockWidth*theMinBlockWidth;

static bool
plotBlock(int &roff, int &coff, int &maxheight,
	GLImage &image, const std::vector<uint32> &data)
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

    if (roff + bheight > 0)
    {
	// Display the block
	for (int i = 0; i < (int)data.size(); i++)
	{
	    int	r, c;
	    theBlockLUT.lookup(r, c, i);
	    r += roff;
	    c += coff;
	    if (r >= 0 && r < image.height() && c < image.width())
		image.setPixel(r, c, data[i]);
	}
    }

    coff += bwidth + theBlockSpacing;

    return true;
}

void
MemoryState::fillRecursiveBlock(GLImage &image, const QPoint &off) const
{
    int		 r = off.y();
    int		 c = 0;
    int		 maxheight = 0;
    std::vector<uint32>	pending;

    StateIterator	it(this);
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	if (it.nempty() >= (uint64)theMinBlockSize)
	{
	    if (pending.size())
	    {
		// Plot the pending block
		if (!plotBlock(r, c, maxheight, image, pending))
		    return;

		// Reset
		pending.clear();
	    }
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
MemoryState::fillImage(GLImage &image, const QPoint &off) const
{
    //StopWatch	 timer;
    image.fill(theBlack);

    switch (myVisualization)
    {
	case LINEAR:
	    fillLinear(image, off);
	    break;
	case BLOCK:
	    fillRecursiveBlock(image, off);
	    break;
    }
}

