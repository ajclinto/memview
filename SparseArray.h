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

#ifndef SparseArray_H
#define SparseArray_H

#include "Math.h"
#include <sys/mman.h>

// Storage for a create-on-write array. The array is mapped into memory but
// does not consume any storage until values are written. This allows
// representation of much larger arrays than would be feasible with
// malloc().  The sparse array also provides an internal hierarchy to help
// reduce the complexity of iteration, with template arguments bottom_bits
// and page_bits to specify this structure.  page_bits specifies the number
// of bits in a page while bottom_bits specifies the number of bits in a
// page plus the number of bits for intermediate existence checks. If you
// have 32 bits for all_bits, good values are bottom_bits=22 and
// page_bits=12, since this provides a good balance between the top,
// bottom, and page levels.
template <typename T, const int bottom_bits, int page_bits>
class SparseArray {
private:
    static const int        theBottomBits = bottom_bits;
    static const uint64     theBottomSize = 1ull << theBottomBits;
    static const uint64     theBottomMask = theBottomSize-1;

    static const int        thePageBits = page_bits;
    static const uint64     thePageSize = 1ull<<thePageBits;
    static const uint64     thePageMask = thePageSize-1;

public:
     // Create an array of size 1<<all_bits
     SparseArray(int all_bits)
     {
         // If all_bits is too small, the reported page size from Page
         // will be incorrect.
         assert(all_bits >= page_bits);

         // This needs to be at least bottom_bits
         all_bits = SYSmax(all_bits, bottom_bits);

         uint64 entries = 1ull << all_bits;

         myTopSize = 1ull << (all_bits - bottom_bits);

         // Map a massive memory buffer to store the state.  This will only
         // translate into physical memory use as we write values to the buffer.
         size_t ssize = entries*sizeof(T);
         size_t dsize = (myTopSize << (bottom_bits-page_bits))*sizeof(bool);
         size_t tsize = myTopSize*sizeof(bool);

         mySize = ssize + tsize + dsize;

         void *addr = mmap(0, mySize,
                 PROT_WRITE | PROT_READ,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                 -1, 0);
         if (addr == MAP_FAILED)
         {
             perror("mmap");
             exit(EXIT_FAILURE);
         }

         myState = (T *)addr;
         myExists = (bool *)((char *)addr + ssize);
         myTopExists = (bool *)((char *)addr + ssize + dsize);
         myPageCount = 0;
     }
    ~SparseArray()
     {
         munmap(myState, mySize);
     }

    void setExists(uint64 addr)
    {
        if (!myExists[addr >> thePageBits])
        {
            myExists[addr >> thePageBits] = true;
            myTopExists[addr >> theBottomBits] = true;
            myPageCount++;
        }
    }

    // Return the number of pages that have been marked as existing with
    // setExists()
    uint64 getPageCount() const { return myPageCount; }

    T              &operator[](uint64 idx) { return myState[idx]; }
    const T        &operator[](uint64 idx) const { return myState[idx]; }

    // Abstract access to a single page
    class Page {
    public:
        Page() : myArr(0), myAddr(0) {}
        Page(T *arr, uint64 addr)
            : myArr(arr)
            , myAddr(addr) {}

        uint64        addr() const        { return myAddr; }
        uint64        size() const        { return thePageSize; }

        T        state(uint64 i) const { return myArr[i]; }
        T        &state(uint64 i) { return myArr[i]; }
        bool      exists() const { return myArr; }

        T        *stateArray()                { return myArr; }
        const T  *stateArray() const        { return myArr; }

    private:
        T            *myArr;
        uint64        myAddr;
    };

    Page        getPage(uint64 addr, uint64 &off) const
    {
        off = addr;
        addr &= ~thePageMask;
        off -= addr;
        return Page(myExists[addr >> thePageBits] ?
                &myState[addr] : 0, addr);
    }

    // A class to iterate over existing pages.
    class Iterator {
    public:
        Iterator(SparseArray<T, bottom_bits, page_bits> &state)
            : myState(state)
            , myTop(0)
            , myBottom(0)
        {
            rewind();
        }

        void    rewind()
                {
                    myTop = 0;
                    myBottom = 0;
                    skipEmpty();
                }
        bool    atEnd() const
                {
                    return myTop >= myState.myTopSize;
                }
        void    advance()
                {
                    myBottom += thePageSize;
                    skipEmpty();
                }

        Page page() const
        {
            uint64 addr = (myTop << theBottomBits) + myBottom;
            return Page(&myState.myState[addr], addr);
        }

    private:
        void    skipEmpty()
                {
                    for (; myTop < myState.myTopSize; myTop++)
                    {
                        if (myState.myTopExists[myTop])
                        {
                            for (; myBottom < theBottomSize;
                                    myBottom += thePageSize)
                            {
                                uint64 didx = ((myTop << theBottomBits) +
                                    myBottom) >> thePageBits;
                                if (myState.myExists[didx])
                                    return;
                            }
                        }
                        myBottom = 0;
                    }
                }

    private:
        SparseArray<T, bottom_bits, page_bits>        &myState;
        uint64                 myTop;
        uint64                 myBottom;
    };

private:
    T           *myState;
    bool        *myTopExists;
    bool        *myExists;
    uint64       myPageCount;
    size_t       mySize;
    uint64       myTopSize;
};

#endif
