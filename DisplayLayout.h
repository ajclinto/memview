#ifndef DisplayLayout_H
#define DisplayLayout_H

#include "Math.h"
#include "GLImage.h"
#include <vector>
#include <stdio.h>

class MemoryState;

class DisplayLayout {
public:
     DisplayLayout();
    ~DisplayLayout();

    enum Visualization {
	LINEAR,
	BLOCK,
	HILBERT
    };

    Visualization   getVisualization() const	{ return myVisualization; }
    void	    setVisualization(Visualization vis)
		    { myVisualization = vis; }

    // Build the block display layout from state
    void	    update(MemoryState &state, int width);

    // Get the resolution of the full layout
    int		    width() const { return myWidth; }
    int		    height() const { return myHeight; }

    // Fill an entire image, starting at the given row and column offset.
    // The 2 available instantiations have additional semantic meaning:
    // uint32 - fills mapped colors
    // uint64 - fills memory addresses
    template <typename T>
    void	    fillImage(GLImage<T> &image,
			  MemoryState &state,
			  int roff, int coff) const;

    // Look up the memory address that corresponds to a given pixel
    uint64	    queryPixelAddress(
			  MemoryState &state,
			  int roff, int coff) const;

private:
    struct DisplayBlock {
	DisplayBlock(uint64 addr, uint64 size)
	    : myAddr(addr)
	    , mySize(size) {}

	uint64	begin() const { return myAddr; }
	uint64	end() const { return myAddr + mySize; }

	uint64	myAddr;
	uint64	mySize;

	Box	myBox;
	int	myStartCol;
    };

    Visualization		myVisualization;
    std::vector<DisplayBlock>	myBlocks;
    int				myWidth;
    int				myHeight;
};

#endif
