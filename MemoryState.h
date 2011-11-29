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

class MemoryState {
public:
     MemoryState();
    ~MemoryState();

    bool	openPipe(int argc, char *argv[]);
    bool	loadFromPipe(int max_read);
    void	fillImage(QImage &image) const;

private:
    typedef uint32	State;

    // The number of low-order bits to ignore.  This value determines the
    // resolution and memory use for the profile.
    static const int	theIgnoreBits = 2;

    static const int	theTopBytes = 16;
    static const uint32	theTopSize = 1 << theTopBytes;
    static const uint32	theTopMask = theTopSize-1;

    static const int	theBottomBytes = 32-theTopBytes;
    static const uint32	theBottomSize = 1 << theBottomBytes;
    static const uint32	theBottomMask = theBottomSize-1;

    int		topIndex(uint32 addr) const
		{ return (addr >> (32-theTopBytes)) & theTopMask; }
    int		bottomIndex(uint32 addr) const
		{ return addr & theBottomMask; }

    State	getEntry(uint32 addr) const
		{
		    State	*row = myTable[topIndex(addr)];
		    return row ? row[bottomIndex(addr)] : 0;
		}
    void	setEntry(uint32 addr, State val)
		{
		    State	*&row = myTable[topIndex(addr)];
		    if (!row)
		    {
			row = new State[theBottomSize];
			memset(row, 0, theBottomSize*sizeof(State));
		    }
		    row[bottomIndex(addr)] = val;
		}

    uint32	mapColor(State val) const
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

		    return myLut[clr];
		}

    // Raw memory state
    State	*myTable[theTopSize];
    State	 myTime;	// Rolling counter

    // Display LUT
    uint32	 myLut[256];

    // Child process
    pid_t	 myChild;
    FILE	*myPipe;
};

#endif
