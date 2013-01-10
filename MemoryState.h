#ifndef MemoryState_H
#define MemoryState_H

#include <QtGui>
#include "Math.h"
#include "GLImage.h"
#include "tool/mv_ipc.h"

// Storage for the entire memory state.  This is specifically designed to
// operate without any locking or atomics for the single writer / many
// reader case.
class MemoryState {
public:
    class State {
    private:
	static const int	theStateShift = 3;
	static const uint32	theStateTypeMask = 0x7;
	static const uint32	theStateTimeMask = ~theStateTypeMask;

    public:
	void init(uint32 time, uint32 type)
       	{ uval = type | (time << theStateShift); }

	void setTime(uint32 time)
	{ init(time, type()); }
	void setFree() { uval |= theTypeFree; }

	uint32 type() const { return uval & theStateTypeMask; }
	uint32 time() const { return uval >> theStateShift; }

	uint32	uval;
    };

    static const uint32	theStale	= 1;
    static const uint32	theFullLife	= 0x1FFFFFFF;
    static const uint32	theHalfLife	= theFullLife >> 1;

private:
    static const int	theAllBits = 36;
    static const uint64	theAllSize = 1ull << theAllBits;
    static const uint64	theAllMask = theAllSize-1;

    static const int	theTopBits = 18;
    static const uint64	theTopSize = 1ull << theTopBits;
    static const uint64	theTopMask = theTopSize-1;

    static const int	theBottomBits = theAllBits-theTopBits;
    static const uint64	theBottomSize = 1ull << theBottomBits;
    static const uint64	theBottomMask = theBottomSize-1;

    // For display - 32x32 is the basic block size
    static const int	theDisplayWidthBits = 6;
    static const uint64	theDisplayWidth = 1ull<<theDisplayWidthBits;
    static const int	theDisplayBits = theDisplayWidthBits<<1;
    static const uint64	theDisplaySize = 1ull<<theDisplayBits;
    static const uint64	theDisplayMask = theDisplaySize-1;
    static const uint64	theDisplayBlocksPerBottom =
			    1ull<<(theBottomBits-theDisplayBits);

public:
     MemoryState(int ignorebits);
    ~MemoryState();

    void	updateAddress(uint64 addr, uint64 size, uint64 type)
		{
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;
		    size = SYSmax(size, 1ull);

		    uint64 last = addr + size;

		    if (!(type & theTypeFree))
		    {
			for (; addr < last; addr++)
			    myState[addr].init(myTime, type);
		    }
		    else
		    {
			for (; addr < last; addr++)
			    myState[addr].setFree();
		    }

		    myExists[addr >> theDisplayBits] = true;
		    myTopExists[addr >> theBottomBits] = true;
		}
    void	incrementTime();
    uint32	getTime() const { return myTime; }
    int		getIgnoreBits() const { return myIgnoreBits; }
    QMutex	*writeLock() { return &myWriteLock; }

    // Print status information for a memory address
    void	printStatusInfo(QString &message, uint64 addr);

    // Build a mipmap from another memory state
    void	downsample(const MemoryState &state);

    // Abstract access to a single display page
    class DisplayPage {
    public:
	DisplayPage(State *arr, uint64 addr)
	    : myArr(arr)
	    , myAddr(addr) {}

	uint64	addr() const	{ return myAddr; }
	uint64	size() const	{ return theDisplaySize; }

	State	state(uint64 i) const { return myArr[i]; }
	State	&state(uint64 i) { return myArr[i]; }
	bool	exists() const { return myArr; }

	State	*stateArray()	{ return myArr; }

    private:
	State	    *myArr;
	uint64	     myAddr;
    };

    DisplayPage	getPage(uint64 addr, uint64 &off) const
    {
	off = addr;
	addr &= ~theDisplayMask;
	off -= addr;
	return DisplayPage(myExists[addr >> theDisplayBits] ?
		&myState[addr] : 0, addr);
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
	    rewind();
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
	{
	    uint64 addr = (myTop << theBottomBits) + myBottom;
	    return DisplayPage(&myState.myState[addr], addr);
	}

    private:
	void	skipEmpty()
		{
		    for (; myTop < theTopSize; myTop++)
		    {
			if (myState.myTopExists[myTop])
			{
			    for (; myBottom < theBottomSize;
				    myBottom += theDisplaySize)
			    {
				uint64 didx = ((myTop << theBottomBits) +
				    myBottom) >> theDisplayBits;
				if (myState.myExists[didx])
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
    State	*myState;
    bool	*myTopExists;
    bool	*myExists;
    size_t	 mySize;

    QMutex	 myWriteLock;
    uint32	 myTime;	// Rolling counter

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;
};

#endif
