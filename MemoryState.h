#ifndef MemoryState_H
#define MemoryState_H

#include <QtGui>
#include "Math.h"

class Loader;
class GLImage;

class MemoryState {
public:
     MemoryState();
    ~MemoryState();

    bool	openPipe(int argc, char *argv[]);

    enum Visualization {
	LINEAR,
	BLOCK
    };
    void	setVisualization(Visualization vis)
		{ myVisualization = vis; }
    void	fillImage(GLImage &image,
			  const QPoint &off, int &height) const;

    void	updateAddress(uint64 addr, int size, char type)
		{
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;
		    size = SYSmax(size, 1);
		    for (int i = 0; i < size; i++)
			setEntry(addr+i, myTime, type);
		}
    void	incrementTime(int inc);

private:
    void	fillLinear(GLImage &image,
			const QPoint &off, int &height) const;
    void	fillRecursiveBlock(GLImage &image,
			const QPoint &off, int &height) const;
    void	plotBlock(int &roff, int &coff, int &maxheight,
			  GLImage &image, uint64 addr, int size) const;

    typedef uint32	State;

    static const State	theStale    = ~(State)0;
    static const State	theHalfLife = theStale>>1;
    static const State	theFullLife = theStale-1;

    static const int	theAllBits = 36;
    static const uint64	theAllSize = 1L << theAllBits;
    static const uint64	theAllMask = theAllSize-1;

    static const int	theTopBits = 18;
    static const uint64	theTopSize = 1L << theTopBits;
    static const uint64	theTopMask = theTopSize-1;

    static const int	theBottomBits = theAllBits-theTopBits;
    static const uint64	theBottomSize = 1L << theBottomBits;
    static const uint64	theBottomMask = theBottomSize-1;

    // For display - 32x32 is the basic block size to ignore
    static const int	theDisplayWidthBits = 5;
    static const uint64	theDisplayWidth = 1<<theDisplayWidthBits;
    static const int	theDisplayBits = theDisplayWidthBits<<1;
    static const uint64	theDisplaySize = 1<<theDisplayBits;
    static const uint64	theDisplayBlocksPerBottom =
			    1<<(theBottomBits-theDisplayBits);

    struct StateArray {
	StateArray()
	{
	    memset(myState, 0, theBottomSize*sizeof(State));
	    memset(myType, 0, theBottomSize*sizeof(char));
	    memset(myExists, 0, theDisplayBlocksPerBottom*sizeof(bool));
	}

	State	myState[theBottomSize];
	char	myType[theBottomSize];
	bool	myExists[theDisplayBlocksPerBottom];
    };

    int		topIndex(uint64 addr) const
		{ return (addr >> theBottomBits) & theTopMask; }
    int		bottomIndex(uint64 addr) const
		{ return addr & theBottomMask; }

    State	getEntry(uint64 addr) const
		{
		    StateArray	*row = myTable[topIndex(addr)];
		    return row ? row->myState[bottomIndex(addr)] : 0;
		}
    void	setEntry(uint64 addr, State val, char type)
		{
		    StateArray	*&row = myTable[topIndex(addr)];
		    int		  idx = bottomIndex(addr);
		    if (!row)
			row = new StateArray;
		    row->myState[idx] = val;
		    row->myType[idx] = type;
		    row->myExists[idx>>theDisplayBits] = true;
		}

    uint32	mapColor(State val, char type) const
		{
		    uint32	diff;

		    diff = val == theStale ? theHalfLife :
			((myTime > val) ? myTime-val+1 : val-myTime+1);

		    diff <<= 8*(sizeof(uint32)-sizeof(State));

		    uint32	bits = __builtin_clz(diff);
		    uint32	clr = bits<<3;

		    if (bits > 28)
			diff <<= bits-28;
		    else
			diff >>= 28-bits;

		    clr |= ~diff & 7;

		    return type == 'I' ? myILut[clr] :
			(type == 'L' ? myRLut[clr] : myWLut[clr]);
		}

    // A class to find contiguous blocks
    class DisplayIterator {
    public:
	DisplayIterator(const MemoryState *state)
	    : myState(state)
	    , myTop(0)
	    , myDisplay(0)
	    , myAddr(0)
	    , mySize(0)
	{
	}

	void	rewind()
		{
		    myTop = 0;
		    myDisplay = 0;
		    myAddr = 0;
		    mySize = 0;
		    advance();
		}
	bool	atEnd() const
		{
		    return myTop >= theTopSize;
		}
	void	advance()
		{
		    mySize = 0;
		    for (; myTop < theTopSize; myTop++)
		    {
			if (table(myTop))
			{
			    for (; myDisplay < theDisplayBlocksPerBottom;
				    myDisplay++)
			    {
				if (table(myTop)->myExists[myDisplay])
				{
				    if (!mySize)
					myAddr = (myTop<<theBottomBits) |
					    (myDisplay<<theDisplayBits);
				    mySize += theDisplaySize;
				}
				else if (mySize)
				    return;
			    }
			}
			else if (mySize)
			    return;
			myDisplay = 0;
		    }
		}

	uint64	addr() const	{ return myAddr; }
	int	size() const	{ return mySize; }

    private:
	const StateArray	*table(int top)	const
				 { return myState->myTable[top]; }

    private:
	const MemoryState	*myState;
	uint64			 myTop;
	uint64			 myDisplay;
	uint64			 myAddr;
	int			 mySize;
    };

    // A class to iterate over only non-zero state values
    class StateIterator {
    public:
	StateIterator(const MemoryState *state)
	    : myState(state)
	    , myTop(0)
	    , myBottom(0)
	    , myEmptyCount(0)
	{
	}

	void	rewind()
		{
		    myTop = 0;
		    myBottom = 0;
		    skipEmpty();
		}
	bool	atEnd() const
		{
		    return myTop >= theTopSize;
		}
	void	advance()
		{
		    myBottom++;
		    skipEmpty();
		}

	State	state() const	{ return table(myTop)->myState[myBottom]; }
	char	type() const	{ return table(myTop)->myType[myBottom]; }
	uint64	nempty() const	{ return myEmptyCount; }

	void	setState(State val)
		{ myState->myTable[myTop]->myState[myBottom] = val; }

    private:
	const StateArray	*table(int top)	const
				 { return myState->myTable[top]; }

	void	skipEmpty()
		{
		    myEmptyCount = 0;
		    for (; myTop < theTopSize; myTop++)
		    {
			if (table(myTop))
			{
			    for (; myBottom < theBottomSize; myBottom++)
			    {
				if (table(myTop)->myState[myBottom])
				    return;
				myEmptyCount++;
			    }
			}
			else
			    myEmptyCount += theBottomSize;
			myBottom = 0;
		    }
		}

    private:
	const MemoryState	*myState;
	uint64			 myTop;
	uint64			 myBottom;
	uint64			 myEmptyCount;
    };

    class QuadTree {
    public:
	QuadTree()
	    : myLeaf(0)
	{
	    memset(mySubtree, 0, 4*sizeof(QuadTree *));
	}
	~QuadTree()
	{
	    for (int i = 0; i < 4; i++)
		delete mySubtree[i];
	}

	void	 addChild(int level, int r, int c, StateArray *arr);
	QSize	 computeSize();
	QSize	 getSize() const	{ return mySize; }

	void	 render(GLImage &image, const QPoint &off, const
			MemoryState &state) const;

    private:
	QSize	 computeWSize(int off);

    private:
	QuadTree	*mySubtree[4];
	StateArray	*myLeaf;
	QSize		 mySize;
    };

private:
    // Raw memory state
    StateArray	*myTable[theTopSize];
    State	 myTime;	// Rolling counter
    uint64	 myHRTime;

    // Loader
    Loader	*myLoader;

    // Display LUT
    uint32	 myILut[256];
    uint32	 myRLut[256];
    uint32	 myWLut[256];

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;

    Visualization	myVisualization;
};

#endif
