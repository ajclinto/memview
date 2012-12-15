#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "GLImage.h"
#include <assert.h>

MemoryState::MemoryState(int ignorebits)
    : myTime(1)
    , myHRTime(1)
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
    if (sizeof(State) == sizeof(uint32))
    {
	myTime++;
    }
    else
    {
	myHRTime++;
	if ((myHRTime & ((1 << 8*(sizeof(uint32)-sizeof(State)))-1)) == 0)
	    myTime++;
    }

    // The time wrapped
    if (myTime == theFullLife || myTime == theHalfLife)
    {
	DisplayIterator	it(*this);
	for (it.rewind(); !it.atEnd(); it.advance())
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
	    myTime = 1;
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
    int shift = myIgnoreBits - state.myIgnoreBits;

    // Copy times first for the display to work correctly
    myTime = state.myTime;
    myHRTime = state.myHRTime;

    DisplayIterator	it(const_cast<MemoryState &>(state));
    for (it.rewind(); !it.atEnd(); it.advance())
    {
	DisplayPage page(it.page());
	for (uint64 i = 0; i < page.size(); i++)
	{
	    State   state = page.state(i);

	    uint64  myaddr = (page.addr() + i) >> shift;
	    uint64  tidx = topIndex(myaddr);
	    uint64  bidx = bottomIndex(myaddr);

	    if (!myTable[tidx])
		myTable[tidx] = new StateArray;
	    if (state.time() > myTable[tidx]->myState[bidx].time())
	    {
		myTable[tidx]->myState[bidx] = state;
		myTable[tidx]->myDirty[bidx >> theDisplayBits] = true;
	    }
	}
    }
}

