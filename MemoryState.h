#ifndef MemoryState_H
#define MemoryState_H

#include <QtGui>
#include "Math.h"

class Loader;
class GLImage;

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

    void	fillImage(GLImage &image, AnchorInfo &info) const;

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
		    addr >>= myIgnoreBits;

		    State	entry = getEntry(addr);
		    char	type = getType(addr);

		    message.sprintf("Address: 0x%.16llx",
			    addr << myIgnoreBits);

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
			    QString	entrystr;
			    entrystr.sprintf(" %12s: %d", typestr, entry);
			    message.append(entrystr);
			    if (islower(type))
				message.append(" (freed)");
			}
		    }
		}

private:
    void	fillLinear(GLImage &image, AnchorInfo &info) const;
    void	fillRecursiveBlock(GLImage &image, AnchorInfo &info) const;

    typedef uint32	State;

    static const State	theStale	= ~(State)0;
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

    uint32	mapColor(State val, char type, int, int) const
		{
		    // LUT indices are computed from the base-2 log of the
		    // access time.  This constant is the number of bits in
		    // the fractional part of this index, based on the LUT
		    // size.
		    const int	frac_bits = theLutBits-5;
		    const int	thresh = 31-frac_bits;
		    uint32	diff;

		    diff = val == theStale ? theHalfLife :
			((myTime > val) ? myTime-val+1 : val-myTime+1);

		    diff <<= 8*(sizeof(uint32)-sizeof(State));

		    int		bits = __builtin_clz(diff);
		    uint32	clr = bits << frac_bits;

		    if (bits > thresh)
			diff <<= bits - thresh;
		    else
			diff >>= thresh - bits;

		    clr |= (~diff) & ((1 << frac_bits)-1);

		    switch (type)
		    {
			case 'i': case 'I':
			    clr = myILut[clr];
			    break;
			case 'l': case 'L':
			    clr = myRLut[clr];
			    break;
			case 's': case 'S':
			case 'm': case 'M':
			    clr = myWLut[clr];
			    break;
			case 'a': case 'A':
			    clr = myALut[clr];
			    break;
		    }

		    // Half the brightness of freed memory
		    if (type >= 'a' && type <= 'z')
			clr = ((clr >> 1) & 0xFF7F7F7F) | 0xFF000000;

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

    // Display LUT size
    static const int	theLutBits = 10;
    static const uint32	theLutSize = 1 << theLutBits;
    static const uint32	theLutMask = theLutSize-1;

    // Display LUT
    uint32	 myILut[theLutSize];
    uint32	 myRLut[theLutSize];
    uint32	 myWLut[theLutSize];
    uint32	 myALut[theLutSize];

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;

    Visualization	myVisualization;

    friend class PlotImage;
};

#endif
