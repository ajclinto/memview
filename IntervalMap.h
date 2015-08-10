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

#ifndef IntervalMap_H
#define IntervalMap_H

#include <QMutex>
#include "Math.h"
#include <unordered_set>
#include <map>
#include <string>
#include <assert.h>
#include <iostream>

template <typename T> class IntervalMapReader;
template <typename T> class IntervalMapWriter;

// This class stores a map of non-overlapping intervals [start, end).  The
// manipulator methods ensure that the interval map is always
// non-overlapping.  Access is through IntervalMapReader and
// IntervalMapWriter to guarantee thread-safety.
template <typename T>
class IntervalMap {
private:
    friend class IntervalMapReader<T>;
    friend class IntervalMapWriter<T>;

    struct Entry {
        uint64   start;
        T        obj;
    };

    typedef std::map<uint64, Entry> MapType;

    MapType           myMap;
    mutable QMutex    myLock;
};

// This class locks access to the interval map, so scope it appropriately.
// This allows multiple operations on the map within the same lock
template <typename T>
class IntervalMapReader {
    typedef typename IntervalMap<T>::MapType MapType;
public:
    IntervalMapReader(const IntervalMap<T> &imap)
        : myMap(imap.myMap)
        , myLock(&imap.myLock) {}

    size_t  size() const { return myMap.size(); }

    class iterator {
    public:
        iterator(typename MapType::const_iterator it) : myIt(it) {}

        uint64         start() const { return myIt->second.start; }
        uint64         end() const { return myIt->first; }
        const T       &value() const { return myIt->second.obj; }

        iterator& operator++() { ++myIt; return *this; }
        iterator operator++(int)
        { iterator tmp(*this); operator++(); return tmp; }

        bool operator==(const iterator& rhs) {return myIt == rhs.myIt;}
        bool operator!=(const iterator& rhs) {return myIt != rhs.myIt;}

    private:
        friend class IntervalMap<T>;
        typename MapType::const_iterator   myIt;
    };

    iterator        begin() const { return iterator(myMap.begin()); }
    iterator        end() const { return iterator(myMap.end()); }

    // Finds the element above and below the query address, and returns the
    // closer of the two.
    iterator    findClosest(uint64 addr) const
    {
        auto hi = myMap.upper_bound(addr);
        if (hi != myMap.end())
        {
            auto lo = hi;
            --lo;

            if (lo != myMap.end() &&
                    dist2(hi, addr) > dist2(lo, addr))
                hi = lo;

            return iterator(hi);
        }
        else if (myMap.size())
        {
            auto lo = hi;
            --lo;
            return iterator(lo);
        }

        return iterator(hi);
    }

    // Returns the element whose interval contains addr if it exists -
    // otherwise 0.
    iterator    find(uint64 addr) const
    {
        iterator rval = findAfter(addr);
        return (rval == end() || rval.start() > addr) ? end() : rval;
    }

    // Returns the first interval that starts after addr or the interval
    // that contains addr.
    iterator    findAfter(uint64 addr) const
    {
        return iterator(myMap.upper_bound(addr));
    }

    // Return the entire interval covered by the map
    void    getTotalInterval(uint64 &start, uint64 &end) const
    {
        if (myMap.size())
        {
            start = myMap.begin()->second.start;
            end = myMap.rbegin()->first;
        }
        else
        {
            start = ~0ull;
            end = 0ull;
        }
    }

    void    dump() const
    {
        for (auto it = myMap.begin(); it != myMap.end(); ++it)
        {
            std::cerr
                << "[" << it->second.start
                << ", " << it->first << "): "
                << it->second.obj << "\n";
        }
    }

private:
    // Find the distance from an address to an interval
    template <typename IT>
    uint64 dist2(const IT &e, uint64 addr) const
    {
        if (addr < e->second.start)
            return e->second.start - addr;
        if (addr >= e->first)
            return addr - e->first + 1;
        return 0;
    }

private:
    const MapType   &myMap;
    QMutexLocker     myLock;
};

// This class locks access to the interval map, so scope it appropriately.
// This allows multiple operations on the map within the same lock
template <typename T>
class IntervalMapWriter : public IntervalMapReader<T> {
    typedef typename IntervalMap<T>::MapType MapType;
public:
    IntervalMapWriter(IntervalMap<T> &imap)
        : IntervalMapReader<T>(imap)
        , myMap(imap.myMap) {}

    void    insert(uint64 start, uint64 end, const T &val)
    {
        clearOverlappingIntervals(start, end);

        myMap[end].start = start;
        myMap[end].obj = val;
    }

    void    erase(uint64 start, uint64 end)
    {
        clearOverlappingIntervals(start, end);
    }

    // Apply the function to all intervals in [start, end).  If any
    // intervals overlap the boundary values, they are split and the
    // function is applied only to the included values.
    template <typename Func>
    void    apply(uint64 start, uint64 end, const Func func)
    {
        typename MapType::iterator first, last;
        getOverlappingIntervals(start, end, first, last);

        for (; first != last; ++first)
            func(first->second.obj);
    }

private:
    // Find all intervals that overlap [start, end).  If any intervals
    // overlapped these boundary values, split the intervals to eliminate
    // overlap.
    void getOverlappingIntervals(
            uint64 start, uint64 end,
            typename MapType::iterator &first,
            typename MapType::iterator &it)
    {
        first = myMap.upper_bound(start);
        for (it = first; it != myMap.end() && it->second.start < end; )
        {
            if (it->second.start < start)
            {
                // We used upper_bound so this should always be true
                assert(it->first > start);

                myMap[start] = it->second;
                it->second.start = start;
            }
            else if (it->first > end)
            {
                myMap[end] = it->second;
                it->second.start = end;
            }
            else
                ++it;
        }
    }

    void clearOverlappingIntervals(uint64 start, uint64 end)
    {
        typename MapType::iterator first, last;
        getOverlappingIntervals(start, end, first, last);
        myMap.erase(first, last);
    }

private:
    MapType   &myMap;
};

#define MAP_TYPE(NAME, TYPE) \
    typedef IntervalMap<TYPE> NAME; \
    typedef IntervalMapReader<TYPE> NAME##Reader; \
    typedef IntervalMapWriter<TYPE> NAME##Writer;

struct StackInfo {
    std::string myStr;
    uint32      myState;
};

struct MMapInfo {
    std::string myStr;
    int         myIdx;
    bool        myMapped;
};

MAP_TYPE(StackTraceMap, StackInfo)
MAP_TYPE(MMapMap, MMapInfo)

#undef MAP_TYPE

#endif
