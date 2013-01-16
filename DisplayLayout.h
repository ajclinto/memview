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

    void	    setCompact(bool compact)
		    { myCompact = compact; }

    // Build the block display layout from state
    void	    update(MemoryState &state,
			   int width, int zoom);

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
	uint64	myDisplayAddr;
	int	myStartCol;
    };

    Visualization		myVisualization;
    std::vector<DisplayBlock>	myBlocks;
    int				myWidth;
    int				myHeight;
    bool			myCompact;
};

#endif
