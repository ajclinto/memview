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

#include "MemoryState.h"
#include "Math.h"
#include "GLImage.h"
#include <vector>
#include <stdio.h>


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
			   MMapMap &mmap,
			   int64 winwidth,
			   int64 width,
			   int zoom);

    // Get the resolution of the full layout
    int64	    width() const { return myWidth; }
    int64	    height() const { return myHeight; }

    // Fill an entire image, starting at the given row and column offset.
    // The Source type determines what data is put in the image.  Currently
    // there are explicit instantiations for:
    //	- uint32, StateSource
    //	- uint64, AddressSource
    //	- uint32, IntervalSource<MMapInfo>
    //  - uint32, IntervalSource<StackInfo>
    template <typename T, typename Source>
    void	    fillImage(GLImage<T> &image,
			  const Source &src,
			  int64 roff, int64 coff) const;

    // Look up the memory address that corresponds to a given pixel
    uint64	    queryPixelAddress(
			  MemoryState &state,
			  int64 roff, int64 coff) const;

private:
    // This method handles the compact display mode in 2D
    template <int dim>
    void	    compactBoxes(int64 &maxval);

private:
    struct DisplayBlock {
	DisplayBlock(uint64 addr, uint64 size)
	    : myAddr(addr)
	    , mySize(size) {}

	uint64	begin() const { return myAddr; }
	uint64	end() const { return myAddr + mySize; }

	uint64	myAddr;
	uint64	mySize;

	Box<int64>	myBox;
	Box<int64>	myDisplayBox;
    };

    Visualization		myVisualization;
    std::vector<DisplayBlock>	myBlocks;
    int64			myWidth;
    int64			myHeight;
    int				myStartLevel;
    int				myStopLevel;
    bool			myCompact;
};

// Fill State values from the given MemoryState
class StateSource {
public:
    StateSource(MemoryState &state) : myState(state) {}

    MemoryState::DisplayPage getPage(uint64 addr, uint64, uint64 &off) const
    { return myState.getPage(addr, off); }

    inline bool exists(const MemoryState::DisplayPage &page) const
    { return page.exists(); }

    inline void setScanline(uint32 *scan,
	    MemoryState::DisplayPage &page, uint64 off, int n) const
    {
	memcpy(scan, page.stateArray() + off, n*sizeof(uint32));
    }
    inline void gatherScanline(uint32 *scan,
	    MemoryState::DisplayPage &page, uint64 off,
	    const int *lut, int n) const
    {
	const uint32	*state = (const uint32 *)page.stateArray() + off;
	for (int i = 0; i < n; i++)
	    scan[i] = state[lut[i]];
    }

private:
    MemoryState	&myState;
};

// Fill memory addresses
class AddressSource {
public:
    AddressSource(MemoryState &state) : myState(state) {}

    MemoryState::DisplayPage getPage(uint64 addr, uint64, uint64 &off) const
    { return myState.getPage(addr, off); }

    inline bool exists(const MemoryState::DisplayPage &) const
    { return true; }

    inline void setScanline(uint64 *scan,
	    MemoryState::DisplayPage &page, uint64 off, int n) const
    {
	for (int i = 0; i < n; i++)
	    scan[i] = page.addr() + off + i;
    }
    inline void gatherScanline(uint64 *scan,
	    MemoryState::DisplayPage &page, uint64 off,
	    const int *lut, int n) const
    {
	for (int i = 0; i < n; i++)
	    scan[i] = page.addr() + off + lut[i];
    }

private:
    MemoryState	&myState;
};

// Fill indices representing which MMap segment each mapped address
// corresponds to
template <typename T>
class IntervalSource {
public:
    IntervalSource(const IntervalMap<T> &intervals,
	    uint64 selection, int ignorebits)
	: myIntervals(intervals)
	, mySelection(selection)
        , myIgnoreBits(ignorebits)
	{}

    struct Page {
	Page() : mySize(0) {}
	Page(uint64 size, bool exists)
	    : mySize(size)
	    , myExists(exists) {}

	uint64 size() const { return mySize; }

	uint64	mySize;
	bool	myExists;
    };

    // These are the values used by the fragment shader
    static inline int getIndex(const MMapInfo &info, bool)
    { return info.myIdx; }
    static inline int getIndex(const StackInfo &info, bool selected)
    { return selected ? 1 : info.myState; }

    Page getPage(uint64 addr, uint64 size, uint64 &off) const
    {
	IntervalMapReader<T>	reader(myIntervals);
	auto	it = reader.findAfter(addr << myIgnoreBits);

	off = 0;

	// addr does not overlap the range - return an empty page
	if (it == reader.end() || (it.start()>>myIgnoreBits) >= addr + size)
	    return Page(size, false);

	myBuffer.assign(size, 0);

	while ((it.start()>>myIgnoreBits) < addr + size)
	{
	    const uint64	a = (1ull << myIgnoreBits) - 1;
	    const bool		selected = mySelection == it.start();
	    uint64		start = it.start() >> myIgnoreBits;
	    uint64		end = (it.end()+a) >> myIgnoreBits;

	    start = SYSmax(start, addr);

	    for (uint64 i = start-addr; i < SYSmin(end-addr, size); i++)
		myBuffer[i] = getIndex(it.value(), selected);

	    ++it;
	    if (it == reader.end())
		break;

	    // When zoomed out there may be many intervals overlapping a
	    // single pixel.  Check if the next interval would end at the
	    // same address and if so perform another binary search to
	    // advance to the next pixel.
	    if (((it.end()+a)>>myIgnoreBits) == end)
	    {
		it = reader.findAfter(end << myIgnoreBits);
		if (it == reader.end())
		    break;
	    }
	}

	return Page(size, true);
    }

    inline bool exists(const Page &page) const { return page.myExists; }

    inline void setScanline(uint32 *scan, Page &, uint64 off, int n) const
    {
	memcpy(scan, &myBuffer[off], n*sizeof(uint32));
    }

    inline void gatherScanline(uint32 *scan,
	    Page &, uint64 off,
	    const int *lut, int n) const
    {
	const uint32	*state = &myBuffer[off];
	for (int i = 0; i < n; i++)
	    scan[i] = state[lut[i]];
    }

private:
    const IntervalMap<T>	  &myIntervals;
    mutable std::vector<uint32>	   myBuffer;
    uint64			   mySelection;
    int	    			   myIgnoreBits;
};

#endif
