#ifndef MemoryState_H
#define MemoryState_H

#include <QtGui>
#include "Math.h"
#include "GLImage.h"

class Loader;

class MemoryState {
public:
    typedef uint32	State;

    static const State	theStale	= 0x1FFFFFFF;
    static const State	theAllocated	= theStale-1;
    static const State	theHalfLife	= theAllocated>>1;
    static const State	theFullLife	= theAllocated-1;

private:
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
    static const uint64	theDisplayMask = theDisplaySize-1;
    static const uint64	theDisplayBlocksPerBottom =
			    1<<(theBottomBits-theDisplayBits);

    struct StateArray {
	StateArray()
	{
	    memset(myState, 0, theBottomSize*sizeof(State));
	    memset(myType, 0, theBottomSize*sizeof(char));
	    memset(myDirty, 0, theDisplayBlocksPerBottom*sizeof(bool));
	    memset(myExists, 0, theDisplayBlocksPerBottom*sizeof(bool));
	}

	State	myState[theBottomSize];
	char	myType[theBottomSize];
	bool	myDirty[theDisplayBlocksPerBottom];
	bool	myExists[theDisplayBlocksPerBottom];
    };

    static int	topIndex(uint64 addr)
		{ return (addr >> theBottomBits) & theTopMask; }
    static int	bottomIndex(uint64 addr)
		{ return addr & theBottomMask; }

    State	getEntry(uint64 addr) const
		{
		    StateArray	*row = myTable[topIndex(addr)];
		    return row ? row->myState[bottomIndex(addr)] : 0;
		}
    char	getType(uint64 addr) const
		{
		    StateArray	*row = myTable[topIndex(addr)];
		    return row ? row->myType[bottomIndex(addr)] : '\0';
		}
    void	setEntry(uint64 addr, State val, char type)
		{
		    StateArray	*&row = myTable[topIndex(addr)];
		    int		  idx = bottomIndex(addr);
		    if (!row)
			row = new StateArray;
		    row->myState[idx] = val;
		    row->myType[idx] = type;
		    row->myDirty[idx>>theDisplayBits] = true;
		}

public:
     MemoryState();
    ~MemoryState();

    bool	openPipe(int argc, char *argv[]);

    void	updateAddress(uint64 addr, int size, char type)
		{
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;
		    size = SYSmax(size, 1);
		    if (type != 'F')
		    {
			for (int i = 0; i < size; i++)
			    setEntry(addr+i, myTime, type);
		    }
		    else
		    {
			for (int i = 0; i < size; i++)
			    setEntry(addr+i, getEntry(addr+i),
				    tolower(getType(addr+i)));
		    }
		}
    void	incrementTime();
    State	getTime() const { return myTime; }

    // Print status information for a memory address
    void	printStatusInfo(QString &message, uint64 addr);

    // Abstract access to a single display page
    class DisplayPage {
    public:
	DisplayPage(StateArray *arr, uint64 top, uint64 bottom)
	    : myArr(arr)
	    , myTop(top)
	    , myBottom(bottom) {}

	uint64	addr() const	{ return (myTop << theBottomBits) | myBottom; }
	int	size() const	{ return theDisplaySize; }

	State	state(int i) const
		{ return myArr->myState[myBottom+i]; }
	char	type(int i) const
		{ return myArr->myType[myBottom+i]; }
	bool	exists() const
		{
		    int didx = myBottom >> theDisplayBits;
		    return myArr &&
			(myArr->myExists[didx] ||
			 myArr->myDirty[didx]);
	       	}

	void	setState(int i, State val)
		{ myArr->myState[myBottom+i] = val; }
	bool	resetDirty()
		{
		    int didx = myBottom >> theDisplayBits;
		    bool dirty = myArr->myDirty[didx];

		    myArr->myDirty[didx] = false;
		    myArr->myExists[didx] = true;
		    return dirty;
		}

    private:
	StateArray  *myArr;
	uint64	     myTop;
	uint64	     myBottom;
    };

    DisplayPage	page(uint64 addr, int &off)
    {
	int tidx = topIndex(addr);
	int bidx = bottomIndex(addr);
	off = bidx & theDisplayMask;
	bidx -= off;
	return DisplayPage(myTable[tidx], tidx, bidx);
    }

    // A class to iterate over only non-zero state values.  The iterator
    // increments in chunks of size theDisplaySize.
    class DisplayIterator {
    public:
	DisplayIterator(MemoryState &state)
	    : myState(state)
	    , myTop(0)
	    , myBottom(0)
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
		    myBottom += theDisplaySize;
		    skipEmpty();
		}

	DisplayPage page() const
	{ return DisplayPage(table(myTop), myTop, myBottom); }

    private:
	StateArray	*table(int top) const { return myState.myTable[top]; }

	void	skipEmpty()
		{
		    for (; myTop < theTopSize; myTop++)
		    {
			if (table(myTop))
			{
			    for (; myBottom < theBottomSize;
				    myBottom += theDisplaySize)
			    {
				int didx = myBottom >> theDisplayBits;
				if (table(myTop)->myExists[didx] ||
				    table(myTop)->myDirty[didx])
				    return;
			    }
			}
			myBottom = 0;
		    }
		}

    private:
	MemoryState	&myState;
	uint64		 myTop;
	uint64		 myBottom;
    };

private:
    // Raw memory state
    StateArray	*myTable[theTopSize];
    State	 myTime;	// Rolling counter
    uint64	 myHRTime;

    // Loader
    Loader	*myLoader;

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;
};

#endif
