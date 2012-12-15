#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "GLImage.h"
#include <assert.h>

MemoryState::MemoryState(int ignorebits)
    : myTime(2)
    , myIgnoreBits(ignorebits)
{
    memset(myTable, 0, theTopSize*sizeof(State *));
}

MemoryState::~MemoryState()
{
    for (uint64 i = 0; i < theTopSize; i++)
	if (myTable[i])
	    delete myTable[i];
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
    switch (type & ~theTypeFree)
    {
	case 0: typestr = "Read"; break;
	case 1: typestr = "Written"; break;
	case 2: typestr = "Instruction"; break;
	case 3: typestr = "Allocated"; break;
    }

    if (typestr)
    {
	tmp.sprintf("\t%12s: %d", typestr, entry.time());
	message.append(tmp);
	if (type & theTypeFree)
	    message.append(" (freed)");
    }
}

void
MemoryState::downsample(const MemoryState &state)
{
    const int shift = myIgnoreBits - state.myIgnoreBits;
    const uint64 scale = 1 << shift;

    // Copy times first for the display to work correctly
    myTime = state.myTime;

    // Update the display bits first
    for (DisplayIterator it(const_cast<MemoryState &>(state));
	    !it.atEnd(); it.advance())
    {
	DisplayPage page(it.page());

	uint64  myaddr = page.addr() >> shift;
	uint64  tidx = topIndex(myaddr);
	uint64  bidx = bottomIndex(myaddr);

	if (!myTable[tidx])
	    myTable[tidx] = new StateArray;
	myTable[tidx]->myDirty[bidx >> theDisplayBits] = true;

	for (uint64 i = 0; i < page.size(); i += scale)
	{
	    State beststate;
	    beststate.uval = 0;

	    State   *arr = page.stateArray();
	    uint64 n = SYSmin(i+scale, page.size());
	    for (uint64 j = i; j < n; j++)
	    {
		beststate.uval = SYSmax(beststate.uval, arr[j].uval);
	    }
	    myTable[tidx]->myState[bidx] = beststate;
	    bidx++;
	}
    }
}

