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
#include <memory>

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
    static const int	theAllBits = 36;
    static const int	thePageBits = 12;

    typedef SparseArray<State, 22, thePageBits> StateArray;

    // Raw memory state
    struct LinkItem {
	LinkItem(uint64 bits, uint64 top, LinkItem *next)
	    : myState(bits)
	    , myTop(top)
	    , myNext(next) {}
	~LinkItem() { delete myNext; }

	StateArray       myState;
	uint64		 myTop;
	LinkItem	*myNext;
    };

    inline void splitAddr(uint64 &addr, uint64 &top) const
    {
	top = addr & myTopMask;
	addr &= myBottomMask;
    }

public:
     MemoryState(int ignorebits);
    ~MemoryState();

#if 1
    class UpdateCache {
    public:
	UpdateCache(MemoryState &state)
	    : myState(state)
	    , myData(&state.myHead.myState)
	    , myTop(state.myHead.myTop)
	    {}

	StateArray &getState(uint64 top)
	{
	    if (__builtin_expect(myTop != top, false))
	    {
		myTop = top;
		myData = &myState.findOrCreateState(top);
	    }
	    return *myData;
	}

    private:
	MemoryState &myState;
	StateArray  *myData;
	uint64	     myTop;
    };
#else
    // Implementation that assumes all memory addresses are within
    // theAllMask, for performance testing
    class UpdateCache {
    public:
	UpdateCache(MemoryState &state) : myState(state) {}

	StateArray &getState(uint64)
	{
	    return myState.myHead.myState;
	}

    private:
	MemoryState &myState;
    };
#endif

    inline void	updateAddress(uint64 addr, uint64 size, uint32 type,
			      UpdateCache &cache)
		{
		    addr >>= myIgnoreBits;
		    size >>= myIgnoreBits;

		    uint64  top = 0;
		    splitAddr(addr, top);

		    StateArray &state = cache.getState(top);
		    state.setExists(addr);

		    uint64 last;
		    switch (size)
		    {
		    case 0:
		    case 1:
			if (!(type & (MV_TypeFree << MV_DataBits)))
			    state[addr].init(myTime, type);
			else
			    state[addr].setFree();
			break;
		    case 2:
			if (!(type & (MV_TypeFree << MV_DataBits)))
			{
			    state[addr].init(myTime, type);
			    state[addr+1].init(myTime, type);
			}
			else
			{
			    state[addr].setFree();
			    state[addr+1].setFree();
			}
			break;
		    default:
			last = addr + size;
			if (!(type & (MV_TypeFree << MV_DataBits)))
			{
			    for (; addr < last; addr++)
				state[addr].init(myTime, type);
			}
			else
			{
			    for (; addr < last; addr++)
				state[addr].setFree();
			}
			break;
		    }
		}

    void	incrementTime(StackTraceMap *stacks = 0);
    uint32	getTime() const { return myTime; }
    int		getIgnoreBits() const { return myIgnoreBits; }

    // Print status information for a memory address
    void	appendAddressInfo(QString &message, uint64 addr,
				  const MMapMap &map);

    // Abstract access to a single page
    class DisplayPage : public StateArray::Page {
    public:
	DisplayPage()
	    : StateArray::Page()
	    , myTop(0) {}
	DisplayPage(const StateArray::Page &src, uint64 top)
	    : StateArray::Page(src)
	    , myTop(top) {}

	uint64	addr() const	{ return myTop | StateArray::Page::addr(); }

    private:
	uint64	     myTop;
    };
    

    DisplayPage	getPage(uint64 addr, uint64 &off) const
    {
	uint64  top;
	splitAddr(addr, top);

	StateArray *state = findState(top);
	if (state)
	    return DisplayPage(state->getPage(addr, off), top);
	off = 0;
	return DisplayPage();
    }

    class DisplayIterator {
    public:
	DisplayIterator(LinkItem *head)
	    : myTop(head)
	{
	    rewind();
	}
	DisplayIterator(const DisplayIterator &it)
	    : myTop(it.myTop)
	{
	    rewind();
	}

	bool	atEnd() const
		{
		    return !myTop;
		}
	void	advance()
		{
		    myBottom->advance();
		    if (myBottom->atEnd())
		    {
			myTop = myTop->myNext;
			rewind();
		    }
		}

	DisplayPage page() const
	{
	    return DisplayPage(myBottom->page(), myTop->myTop);
	}

    private:
	void	rewind()
		{
		    if (myTop)
		    {
			myBottom.reset(
				new StateArray::Iterator(myTop->myState));
		    }
		}

    private:
	LinkItem				*myTop;
	std::unique_ptr<StateArray::Iterator>	 myBottom;
    };

    DisplayIterator begin()
    {
	return DisplayIterator(&myHead);
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

    LinkItem	*findLink(uint64 top, LinkItem *&prev) const
    {
	LinkItem    *it = const_cast<LinkItem *>(&myHead);

	prev = 0;
	while (it && it->myTop < top)
	{
	    prev = it;
	    it = it->myNext;
	}

	return it;
    }

    StateArray	*findState(uint64 top) const
    {
	LinkItem    *prev;
	LinkItem    *it = findLink(top, prev);
	return (it && it->myTop == top) ? &it->myState : 0;
    }

    StateArray	&findOrCreateState(uint64 top)
    {
	LinkItem    *prev;
	LinkItem    *it = findLink(top, prev);

	if (it && it->myTop == top)
	    return it->myState;

	// Double checked lock
	QMutexLocker	lock(&myWriteLock);
	it = findLink(top, prev);
	if (it && it->myTop == top)
	    return it->myState;

	it = new LinkItem(myBottomBits, top, it);
	if (prev)
	    prev->myNext = it;

	return it->myState;
    }

private:
    QMutex	 myWriteLock;
    uint32	 myTime;	// Rolling counter

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;
    int		 myBottomBits;
    uint64	 myBottomMask;
    uint64	 myTopMask;

    // Maps memory for mask 0 on creation
    LinkItem	 myHead;
};

#endif
