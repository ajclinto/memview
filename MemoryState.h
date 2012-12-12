#ifndef MemoryState_H
#define MemoryState_H

#include <QtGui>
#include "Math.h"
#include "GLImage.h"
#include "tool/mv_ipc.h"

class MemoryState {
public:
    class State {
    private:
	static const int	theStateShift = 29;
	static const uint32	theStateTimeMask = (1 << 29) - 1;
	static const uint32	theStateTypeMask = ~theStateTimeMask;

    public:
	void init(uint32 time, int type)
       	{ uval = time; uval |= type << theStateShift; }

	void setTime(uint32 time) { uval &= theStateTypeMask; uval |= time; }
	void setFree() { uval |= theTypeFree << theStateShift; }

	int type() const { return uval >> theStateShift; }
	int time() const { return uval & theStateTimeMask; }

	uint32	uval;
    };

    static const uint32	theStale	= 0x1FFFFFFF;
    static const uint32	theFullLife	= theStale-1;
    static const uint32	theHalfLife	= theFullLife >> 1;

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

    // For display - 32x32 is the basic block size
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
	    memset(myDirty, 0, theDisplayBlocksPerBottom*sizeof(bool));
	    memset(myExists, 0, theDisplayBlocksPerBottom*sizeof(bool));
	}

	State	myState[theBottomSize];
	bool	myDirty[theDisplayBlocksPerBottom];
	bool	myExists[theDisplayBlocksPerBottom];
    };

    static uint64	topIndex(uint64 addr)
			{ return (addr >> theBottomBits) & theTopMask; }
    static uint64	bottomIndex(uint64 addr)
			{ return addr & theBottomMask; }

public:
     MemoryState(int ignorebits);
    ~MemoryState();

    void	updateAddress(uint64 addr, int size, int type)
		{
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;
		    size = SYSmax(size, 1);

		    while (size)
		    {
			StateArray	*&row = myTable[topIndex(addr)];
			uint64		  idx = bottomIndex(addr);

			if (!row)
			    row = new StateArray;

			uint64 last = idx + size;

			size = 0;

			// The address crossed a page boundary?
			if (last > theBottomSize)
			{
			    // Update remaining size
			    size = last - theBottomSize;
			    last = theBottomSize;
			    addr += theBottomSize-idx;
			}

			if (!(type & theTypeFree))
			{
			    for (; idx < last; idx++)
			    {
				row->myState[idx].init(myTime, type);
				row->myDirty[idx>>theDisplayBits] = true;
			    }
			}
			else
			{
			    for (; idx < last; idx++)
			    {
				row->myState[idx].setFree();
				row->myDirty[idx>>theDisplayBits] = true;
			    }
			}
		    }
		}
    void	incrementTime();
    uint32	getTime() const { return myTime; }

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
	uint64	size() const	{ return theDisplaySize; }

	State	state(uint64 i) const { return myArr->myState[myBottom+i]; }
	State	&state(uint64 i) { return myArr->myState[myBottom+i]; }
	bool	exists() const
		{
		    uint64 didx = myBottom >> theDisplayBits;
		    return myArr &&
			(myArr->myExists[didx] ||
			 myArr->myDirty[didx]);
	       	}

	bool	resetDirty()
		{
		    uint64  didx = myBottom >> theDisplayBits;
		    bool    dirty = myArr->myDirty[didx];

		    myArr->myDirty[didx] = false;
		    myArr->myExists[didx] = true;
		    return dirty;
		}

	State	*stateArray()	{ return &myArr->myState[myBottom]; }

    private:
	StateArray  *myArr;
	uint64	     myTop;
	uint64	     myBottom;
    };

    DisplayPage	getPage(uint64 addr, uint64 &off)
    {
	uint64 tidx = topIndex(addr);
	uint64 bidx = bottomIndex(addr);
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
	StateArray	*table(uint64 top) const
			 { return myState.myTable[top]; }

	void	skipEmpty()
		{
		    for (; myTop < theTopSize; myTop++)
		    {
			if (table(myTop))
			{
			    for (; myBottom < theBottomSize;
				    myBottom += theDisplaySize)
			    {
				uint64 didx = myBottom >> theDisplayBits;
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
    uint32	 myTime;	// Rolling counter
    uint64	 myHRTime;

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;
};

#endif
