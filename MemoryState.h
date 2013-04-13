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

#ifndef MemoryState_H
#define MemoryState_H

#include "Math.h"
#include "GLImage.h"
#include "IntervalMap.h"
#include "valgrind/memview/mv_ipc.h"

// Storage for the entire memory state.  This is specifically designed to
// operate without any locking or atomics for the single writer / many
// reader case.
class MemoryState {
public:
    class State {
    public:
	static const int	theStateShift = 17;

    private:
	// Here, type is the combined metadata that excludes the time
	static const uint32	theStateTypeMask = (1 << theStateShift) - 1;
	static const uint32	theStateTimeMask = ~theStateTypeMask;

	// Sub-fields of type
	static const int	theSubDataBits = 3;
	static const uint32	theSubDataMask = (1 << theSubDataBits) - 1;
	static const int	theSubTypeBits = 3;
	static const uint32	theSubTypeMask = (1 << theSubTypeBits) - 1;
	static const int	theSubThreadBits = 10;
	static const uint32	theSubThreadMask = (1 << theSubThreadBits) - 1;
	static const uint32	theSubSelectedMask = 1 << (theStateShift-1);

    public:
	void init(uint32 time, uint32 type)
       	{ uval = type | (time << theStateShift); }

	void setTime(uint32 time)
	{ uval = (uval & theStateTypeMask) | (time << theStateShift); }
	void setFree() { uval |= (MV_TypeFree << MV_DataBits); }
	void setSelected() { uval |= theSubSelectedMask; }

	// Field accessors.  Here type is the sub-type (without the thread
	// id).
	uint32 dtype() const { return uval & theSubDataMask; }
	uint32 type() const { return (uval >> theSubDataBits) &
					    theSubTypeMask; }
	uint32 thread() const { return (uval >> (theSubDataBits +
					    theSubTypeBits)) &
					    theSubThreadMask; }
	uint32 selected() const { return uval & theSubSelectedMask; }
	uint32 time() const { return uval >> theStateShift; }

	uint32	uval;
    };

    static const uint32	theStale	= 1;
    static const uint32	theFullLife	= 1 << (32-State::theStateShift);
    static const uint32	theHalfLife	= theFullLife >> 1;

private:
    // The maximum space is 36 bits or 64Gb of memory.
    static const int	theAllBits = 36;
    static const uint64	theAllSize = 1ull << theAllBits;
    static const uint64	theAllMask = theAllSize-1;

    static const int	theTopBits = 18;
    static const uint64	theTopSize = 1ull << theTopBits;
    static const uint64	theTopMask = theTopSize-1;

    static const int	theBottomBits = theAllBits-theTopBits;
    static const uint64	theBottomSize = 1ull << theBottomBits;
    static const uint64	theBottomMask = theBottomSize-1;

    // For display - 64x64 is the basic block size
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
		    addr &= theAllMask;
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;

		    myExists[addr >> theDisplayBits] = true;
		    myTopExists[addr >> theBottomBits] = true;

		    uint64 last;
		    switch (size)
		    {
		    case 0:
		    case 1:
			if (!(type & (MV_TypeFree << MV_DataBits)))
			    myState[addr].init(myTime, type);
			else
			    myState[addr].setFree();
			break;
		    case 2:
			if (!(type & (MV_TypeFree << MV_DataBits)))
			{
			    myState[addr].init(myTime, type);
			    myState[addr+1].init(myTime, type);
			}
			else
			{
			    myState[addr].setFree();
			    myState[addr+1].setFree();
			}
			break;
		    default:
			last = addr + size;
			if (!(type & (MV_TypeFree << MV_DataBits)))
			{
			    for (; addr < last; addr++)
				myState[addr].init(myTime, type);
			}
			else
			{
			    for (; addr < last; addr++)
				myState[addr].setFree();
			}
			break;
		    }
		}

    // Set all display flags in the given range
    void	setRangeExists(uint64 start, uint64 end)
		{
		    start &= theAllMask;
		    start >>= myIgnoreBits;
		    end &= theAllMask;
		    end >>= myIgnoreBits;

		    while (start <= end)
		    {
			myExists[start >> theDisplayBits] = true;
			myTopExists[start >> theBottomBits] = true;
			start += 1ull << theDisplayBits;
		    }
		}

    void	incrementTime(StackTraceMap *stacks = 0);
    uint32	getTime() const { return myTime; }
    int		getIgnoreBits() const { return myIgnoreBits; }
    QMutex	*writeLock() { return &myWriteLock; }

    // Print status information for a memory address
    void	appendAddressInfo(QString &message, uint64 addr,
				  const MMapMap &map);

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

	State		*stateArray()		{ return myArr; }
	const State	*stateArray() const	{ return myArr; }

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

    // Build a mipmap from another memory state
    void	downsample(const MemoryState &state);
    void	downsamplePage(const DisplayPage &page, int shift, bool fast);

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
