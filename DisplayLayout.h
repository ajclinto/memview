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
			   const MMapMap &mmapmap,
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
    //  - uint32, IntervalSource<std::string>
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
	Page(uint64 size, uint32 idx)
	    : mySize(size)
	    , myIdx(idx) {}

	uint64 size() const { return mySize; }

	uint64	mySize;
	uint32	myIdx;
    };

    static inline int getIndex(const MMapInfo &info, bool)
    { return info.myIdx; }
    static inline int getIndex(const std::string &, bool selected)
    { return selected ? 2 : 1; }

    Page getPage(uint64 addr, uint64 size, uint64 &off) const
    {
	uint64  start, end;
	T	info = myIntervals.findAfter(addr << myIgnoreBits, start, end);
	bool	selected = mySelection == start;

	start >>= myIgnoreBits;
	end += (1 << myIgnoreBits) - 1;
	end >>= myIgnoreBits;

	// addr does not overlap the range - return an empty page
	off = 0;
	if (end <= addr)
	    return Page(size, 0);
	if (start > addr)
	    return Page(start-addr, 0);

	off = addr - start;
	return Page(end-start, getIndex(info, selected));
    }

    inline bool exists(const Page &page) const { return page.myIdx; }

    inline void setScanline(uint32 *scan, Page &page, uint64, int n) const
    { std::fill_n(scan, n, page.myIdx); }

    inline void gatherScanline(uint32 *scan,
	    Page &page, uint64,
	    const int *, int n) const
    { std::fill_n(scan, n, page.myIdx); }

private:
    const IntervalMap<T>  &myIntervals;
    uint64		   mySelection;
    int			   myIgnoreBits;
};

#endif
