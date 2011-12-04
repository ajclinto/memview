#ifndef MemoryState_H

#include <sys/types.h>
#include <signal.h>
#include <QtGui>

typedef unsigned		uint32;
typedef unsigned long long	uint64;

inline uint32 SYSmax(uint32 a, uint32 b)
{
    return a > b ? a : b;
}
inline uint32 SYSmin(uint32 a, uint32 b)
{
    return a < b ? a : b;
}
inline uint32 SYSclamp(uint32 v, uint32 a, uint32 b)
{
    return v < a ? a : (v > b ? b : v);
}
inline float SYSlerp(float v1, float v2, float bias)
{
    return v1 + bias*(v2-v1);
}

class MemoryState {
public:
     MemoryState();
    ~MemoryState();

    bool	openPipe(int argc, char *argv[]);
    bool	loadFromPipe(int max_read);

    enum Visualization {
	LINEAR,
	BLOCK
    };
    void	setVisualization(Visualization vis)
		{ myVisualization = vis; }
    void	fillImage(QImage &image) const;

private:
    void	fillLinear(QImage &image) const;
    void	fillRecursiveBlock(QImage &image) const;

    typedef uint32	State;

    static const int	theAllBits = 36;
    static const uint64	theAllSize = 1L << theAllBits;
    static const uint64	theAllMask = theAllSize-1;

    static const int	theTopBits = 18;
    static const uint64	theTopSize = 1L << theTopBits;
    static const uint64	theTopMask = theTopSize-1;

    static const int	theBottomBits = theAllBits-theTopBits;
    static const uint64	theBottomSize = 1L << theBottomBits;
    static const uint64	theBottomMask = theBottomSize-1;

    struct StateArray {
	StateArray()
	{
	    memset(myState, 0, theBottomSize*sizeof(State));
	    memset(myType, 0, theBottomSize*sizeof(char));
	}

	State	myState[theBottomSize];
	char	myType[theBottomSize];
    };

    int		topIndex(uint64 addr) const
		{ return (addr >> (32-theTopBits)) & theTopMask; }
    int		bottomIndex(uint64 addr) const
		{ return addr & theBottomMask; }

    State	getEntry(uint64 addr) const
		{
		    StateArray	*row = myTable[topIndex(addr)];
		    return row ? row->myState[bottomIndex(addr)] : 0;
		}
    void	setEntry(uint64 addr, State val, char type)
		{
		    StateArray	*&row = myTable[topIndex(addr)];
		    int		  idx = bottomIndex(addr);
		    if (!row)
			row = new StateArray;
		    row->myState[idx] = val;
		    row->myType[idx] = type;
		}

    uint32	mapColor(State val, char type) const
		{
		    uint32	diff;

		    if (myTime >= val)
			diff = myTime - val + 1;
		    else
			diff = val - myTime + 1;

		    uint32	bits = __builtin_clz(diff);
		    uint32	clr = bits*8;

		    if (bits > 28)
			clr += ~(diff << (bits-28)) & 7;
		    else
			clr += ~(diff >> (28-bits)) & 7;

		    return type == 'I' ? myILut[clr] :
			(type == 'L' ? myRLut[clr] : myWLut[clr]);
		}

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

    // Display LUT
    uint32	 myILut[256];
    uint32	 myRLut[256];
    uint32	 myWLut[256];

    // Child process
    pid_t	 myChild;
    FILE	*myPipe;

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    int		 myIgnoreBits;

    Visualization	myVisualization;
};

#endif
