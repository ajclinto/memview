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

#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "GLImage.h"
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>

MemoryState::MemoryState(int ignorebits)
    : myTime(2)
    , myIgnoreBits(ignorebits)
{
    // Map a massive memory buffer to store the state.  This will only
    // translate into physical memory use as we write values to the buffer.
    size_t ssize = theAllSize*sizeof(State);
    size_t dsize = theTopSize*theDisplayBlocksPerBottom*sizeof(bool);
    size_t tsize = theTopSize*sizeof(bool);

    mySize = ssize + tsize + dsize;

    void *addr = mmap(0, mySize,
	    PROT_WRITE | PROT_READ,
	    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NONBLOCK | MAP_NORESERVE,
	    0, 0);

    myState = (State *)addr;
    myExists = (bool *)((char *)addr + ssize);
    myTopExists = (bool *)((char *)addr + ssize + dsize);
}

MemoryState::~MemoryState()
{
    munmap(myState, mySize);
}

void
MemoryState::incrementTime()
{
    myTime++;

    // The time wrapped
    if (myTime == theFullLife || myTime == theHalfLife)
    {
	for (DisplayIterator it(*this); !it.atEnd(); it.advance())
	{
	    DisplayPage page(it.page());
	    for (uint64 i = 0; i < page.size(); i++)
	    {
		uint32	state = page.state(i).time();
		if ((myTime == theFullLife && state < theHalfLife) ||
		    (myTime == theHalfLife && state >= theHalfLife &&
		     state <= theFullLife))
		    page.state(i).setTime(theStale);
	    }
	}
	if (myTime == theFullLife)
	    myTime = 2;
	else
	    myTime++;
    }
}

void
MemoryState::printStatusInfo(QString &message, uint64 addr)
{
    message.sprintf("Batch: %4d", myTime);

    if (!addr)
	return;

    QString	tmp;
    tmp.sprintf("\t\tAddress: 0x%.16llx", addr << myIgnoreBits);

    message.append(tmp);

    uint64	off;
    auto	page = getPage(addr, off);
    State	entry = page.state(off);

    if (!entry.uval)
	return;

    const char	*typestr = 0;

    int type = entry.type();
    switch (type & ~MV_TypeFree)
    {
	case MV_TypeRead: typestr = "Read"; break;
	case MV_TypeWrite: typestr = "Written"; break;
	case MV_TypeInstr: typestr = "Instruction"; break;
	case MV_TypeAlloc: typestr = "Allocated"; break;
    }

    if (typestr)
    {
	tmp.sprintf("\t%12s: %d", typestr, entry.time());
	message.append(tmp);
	if (type & MV_TypeFree)
	    message.append(" (freed)");
    }
}

void
MemoryState::downsample(const MemoryState &state)
{
    const int shift = myIgnoreBits - state.myIgnoreBits;
    const uint64 scale = 1 << shift;

    // Copy time first for the display to work correctly
    myTime = state.myTime;

    for (DisplayIterator it(const_cast<MemoryState &>(state));
	    !it.atEnd(); it.advance())
    {
	DisplayPage page(it.page());

	uint64  myaddr = page.addr() >> shift;

	myTopExists[myaddr >> theBottomBits] = true;
	myExists[myaddr >> theDisplayBits] = true;

	for (uint64 i = 0; i < page.size(); i += scale)
	{
	    uint    &mystate = myState[myaddr].uval;
	    State   *arr = page.stateArray();
	    uint64   n = SYSmin(i+scale, page.size());
	    for (uint64 j = i; j < n; j++)
	    {
		mystate = SYSmax(mystate, arr[j].uval);
	    }
	    myaddr++;
	}
    }
}

