#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "Loader.h"
#include "GLImage.h"
#include <assert.h>

MemoryState::MemoryState()
    : myTime(1)
    , myHRTime(1)
    , myLoader(0)
    , myIgnoreBits(2)
{
    memset(myTable, 0, theTopSize*sizeof(State *));
}

MemoryState::~MemoryState()
{
    delete myLoader;

    for (uint64 i = 0; i < theTopSize; i++)
	if (myTable[i])
	    delete [] myTable[i];
}

bool
MemoryState::openPipe(int argc, char *argv[])
{
    const char	*ignore = extractOption(argc, argv, "--ignore-bits=");

    if (ignore)
	myIgnoreBits = atoi(ignore);

    myLoader = new Loader(this);

    if (myLoader->openPipe(argc, argv))
    {
	// Start loading data in a new thread
	myLoader->start();
	return true;
    }

    return false;
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
	    for (int i = 0; i < page.size(); i++)
	    {
		State	state = page.state(i);
		if ((myTime == theFullLife && state < theHalfLife) ||
		    (myTime == theHalfLife && state >= theHalfLife &&
		     state <= theFullLife))
		    page.setState(i, theStale);
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
    tmp.sprintf("\t\tAddress: 0x%.16llx", addr);

    message.append(tmp);

    addr >>= myIgnoreBits;

    State	entry = getEntry(addr);
    char	type = getType(addr);

    if (!entry)
	return;

    if (entry)
    {
	const char	*typestr = 0;
	switch (type)
	{
	    case 'i': case 'I':
		typestr = "Instruction";
		break;
	    case 'l': case 'L':
		typestr = "Read";
		break;
	    case 's': case 'S':
	    case 'm': case 'M':
		typestr = "Written";
		break;
	    case 'a': case 'A':
		typestr = "Allocated";
		break;
	}

	if (typestr)
	{
	    tmp.sprintf("\t%12s: %d", typestr, entry);
	    message.append(tmp);
	    if (islower(type))
		message.append(" (freed)");
	}
    }
}
