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
#include "SparseArray.h"
#include "mv_ipc.h"

// Storage for the entire memory state.  This is specifically designed to
// operate without any locking or atomics for the single writer / many
// reader case.
class MemoryState {
public:
    class State {
    public:
	static const int	theTimeShift = 17;

    private:
	// Here, type is the combined metadata that excludes the time
	static const uint32	theStateTypeMask = (1 << theTimeShift) - 1;
	static const uint32	theStateTimeMask = ~theStateTypeMask;

	// Sub-fields of type
	static const int	theSubDataBits = 3;
	static const uint32	theSubDataMask = (1 << theSubDataBits) - 1;
	static const int	theSubTypeBits = 3;
	static const uint32	theSubTypeMask = (1 << theSubTypeBits) - 1;
	static const int	theSubThreadBits = 10;
	static const uint32	theSubThreadMask = (1 << theSubThreadBits) - 1;
	static const uint32	theSubSelectedMask = 1 << (theTimeShift-1);

    public:
	void init(uint32 time, uint32 type)
       	{ uval = type | (time << theTimeShift); }

	void setTime(uint32 time)
	{ uval = (uval & theStateTypeMask) | (time << theTimeShift); }

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
	uint32 time() const { return uval >> theTimeShift; }

	uint32	uval;
    };

    static const uint32	theStale	= 1;
    static const uint32	theFullLife	= 1 << (32-State::theTimeShift);
    static const uint32	theHalfLife	= theFullLife >> 1;

private:
    // The maximum space is 36 bits or 64Gb of memory.
    static const int	theAllBits = 36;
    static const uint64	theAllSize = 1ull << theAllBits;
    static const uint64	theAllMask = theAllSize-1;

    typedef SparseArray<State, 22, 12> StateArray;

public:
     MemoryState(int ignorebits);
    ~MemoryState();

    void	updateAddress(uint64 addr, uint64 size, uint64 type)
		{
		    addr &= theAllMask;
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;

		    myState.setExists(addr);

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

    void	incrementTime(StackTraceMap *stacks = 0);
    uint32	getTime() const { return myTime; }
    int		getIgnoreBits() const { return myIgnoreBits; }
    QMutex	*writeLock() { return &myWriteLock; }

    // Print status information for a memory address
    void	appendAddressInfo(QString &message, uint64 addr,
				  const MMapMap &map);

    typedef StateArray::Page DisplayPage;
    typedef StateArray::Iterator DisplayIterator;

    DisplayPage	getPage(uint64 addr, uint64 &off) const
    {
	return myState.getPage(addr, off);
    }
    DisplayIterator begin()
    {
	return DisplayIterator(myState);
    }

    // Build a mipmap from another memory state
    void	downsample(const MemoryState &state);
    void	downsamplePage(const DisplayPage &page, int shift, bool fast);

private:
    class StackInfoUpdater {
	bool myFull;
    public:
	StackInfoUpdater(bool full) : myFull(full) {}
	void operator()(StackInfo &val) const
	{
	    State  sval; sval.uval = val.myState;
	    uint32 state = sval.time();

	    if (state && ((state >= theHalfLife) ^ myFull))
	    {
		sval.setTime(theStale);
		val.myState = sval.uval;
	    }
	}
    };

private:
    // Raw memory state
    StateArray	 myState;

    QMutex	 myWriteLock;
    uint32	 myTime;	// Rolling counter

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;
};

#endif
