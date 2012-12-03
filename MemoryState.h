#ifndef MemoryState_H
#define MemoryState_H

#include <QtGui>
#include "Math.h"
#include "GLImage.h"

class Loader;

class MemoryState {
public:
     MemoryState();
    ~MemoryState();

    bool	openPipe(int argc, char *argv[]);

    enum Visualization {
	LINEAR,
	BLOCK,
	HILBERT
    };
    Visualization   getVisualization() const	{ return myVisualization; }
    void	    setVisualization(Visualization vis)
		    { myVisualization = vis; }

    // This struct is used as input and output for the fillImage routine,
    // to indicate the preferred rendering position and also to report back
    // accumulated information for the scrollbars.
    struct AnchorInfo {
	AnchorInfo()
	    : myAnchorAddr(0)
	    , myAnchorOffset(0)
	    , myAbsoluteOffset(0)
	    , myHeight(0)
	    , myWidth(0)
	    , myQueryAddr(0)
	    , myColumn(0)
	    {}

	// ** Input/Output
	// A memory address that should be placed in a fixed vertical
	// location on the current page
	uint64	myAnchorAddr;
	// The relative display offset from the anchor address in pixels
	int	myAnchorOffset;

	// ** Output only
	// The absolute vertical location of the first visible row of state
	int	myAbsoluteOffset;
	// The full image resolution
	int	myHeight;
	int	myWidth;
	// The query address
	uint64	myQueryAddr;

	// ** Input only
	int	myColumn;
	QPoint	myQuery;
    };

    void	fillImage(GLImage<uint32> &image, AnchorInfo &info) const;

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

    // Print status information for a memory address
    void	printStatusInfo(QString &message, uint64 addr)
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

private:
    void	fillLinear(GLImage<uint32> &image, AnchorInfo &info) const;
    void	fillRecursiveBlock(GLImage<uint32> &image, AnchorInfo &info) const;

    typedef uint32	State;

    static const State	theStale	= 0x1FFFFFFF;
    static const State	theAllocated	= theStale-1;
    static const State	theHalfLife	= theAllocated>>1;
    static const State	theFullLife	= theAllocated-1;

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
    static const uint64	theDisplayBlocksPerBottom =
			    1<<(theBottomBits-theDisplayBits);

    struct StateArray {
	StateArray()
	{
	    memset(myState, 0, theBottomSize*sizeof(State));
	    memset(myType, 0, theBottomSize*sizeof(char));
	    memset(myExists, 0, theDisplayBlocksPerBottom*sizeof(bool));
	}

	State	myState[theBottomSize];
	char	myType[theBottomSize];
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
		    row->myExists[idx>>theDisplayBits] = true;
		}

    uint32	mapColor(State val, char type) const
		{
		    uint32 clr = val;

		    uint32 lut = 0;
		    switch (type)
		    {
			case 'l': case 'L':
			    lut = 0;
			    break;
			case 's': case 'S':
			case 'm': case 'M':
			    lut = 1;
			    break;
			case 'i': case 'I':
			    lut = 2;
			    break;
			case 'a': case 'A':
			    lut = 3;
			    break;
		    }

		    clr |= lut << 30;

		    // Half the brightness of freed memory
		    if (type >= 'a' && type <= 'z')
			clr |= 1 << 29;

		    return clr;
		}

    // A class to find contiguous blocks
    class DisplayIterator {
    public:
	DisplayIterator(const MemoryState *state)
	    : myState(state)
	    , myTop(0)
	    , myDisplay(0)
	    , myAddr(0)
	    , mySize(0)
	    , myEmpty(0)
	    , myVacant(0)
	{
	}

	void	rewind(uint64 addr = 0)
		{
		    myTop = topIndex(addr);
		    myDisplay = bottomIndex(addr) >> theDisplayBits;
		    myAddr = addr;
		    mySize = 0;
		    myEmpty = 0;
		    myVacant = 0;
		    advance();
		}
	bool	atEnd() const
		{
		    return myTop >= theTopSize;
		}
	void	advance()
		{
		    mySize = 0;
		    myEmpty = 0;
		    myVacant = 0;
		    for (; myTop < theTopSize; myTop++)
		    {
			if (table(myTop))
			{
			    for (; myDisplay < theDisplayBlocksPerBottom;
				    myDisplay++)
			    {
				if (table(myTop)->myExists[myDisplay])
				{
				    if (!mySize)
					myAddr = (myTop<<theBottomBits) |
					    (myDisplay<<theDisplayBits);
				    mySize += theDisplaySize;
				    mySize += myEmpty;
				    myEmpty = 0;
				}
				else if (mySize)
				{
				    myEmpty += theDisplaySize;
				    myVacant += theDisplaySize;
				    if (myVacant > (mySize>>3))
					return;
				}
			    }
			}
			else if (mySize)
			    return;
			myDisplay = 0;
		    }
		}

	uint64	addr() const	{ return myAddr; }
	int	size() const	{ return mySize; }

    private:
	const StateArray	*table(int top)	const
				 { return myState->myTable[top]; }

    private:
	const MemoryState	*myState;
	uint64			 myTop;
	uint64			 myDisplay;
	uint64			 myAddr;
	int			 mySize;    // Display block size
	int			 myEmpty;   // Contiguous empties
	int			 myVacant;  // Total empties seen
    };

    // A class to iterate over only non-zero state values
    class StateIterator {
    public:
	StateIterator(const MemoryState *state)
	    : myState(state)
	    , myTop(0)
	    , myBottom(0)
	    , myEmptyCount(0)
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
		    myBottom++;
		    skipEmpty();
		}

	State	state() const	{ return table(myTop)->myState[myBottom]; }
	char	type() const	{ return table(myTop)->myType[myBottom]; }
	uint64	nempty() const	{ return myEmptyCount; }

	void	setState(State val)
		{ myState->myTable[myTop]->myState[myBottom] = val; }

    private:
	const StateArray	*table(int top)	const
				 { return myState->myTable[top]; }

	void	skipEmpty()
		{
		    myEmptyCount = 0;
		    for (; myTop < theTopSize; myTop++)
		    {
			if (table(myTop))
			{
			    for (; myBottom < theBottomSize; myBottom++)
			    {
				if (table(myTop)->myState[myBottom])
				    return;
				myEmptyCount++;
			    }
			}
			else
			    myEmptyCount += theBottomSize;
			myBottom = 0;
		    }
		}

    private:
	const MemoryState	*myState;
	uint64			 myTop;
	uint64			 myBottom;
	uint64			 myEmptyCount;
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

    Visualization	myVisualization;

    friend class PlotImage;
    friend class MemViewWidget;
};

#endif
