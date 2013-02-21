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

#ifndef SparseMap_H
#define SparseMap_H

#include <QMutex>
#include "Math.h"
#include <unordered_set>
#include <map>
#include <string>

template <typename T>
class SparseMap {
public:
    void    insert(uint64 addr, const T &val)
    {
	QMutexLocker lock(&myLock);
	myMap[addr] = val;
    }

    T	    *findClosest(uint64 addr)
    {
	QMutexLocker lock(&myLock);
	// Finds the element above and below the query address, and returns
	// the closer of the two.
	auto hi = myMap.lower_bound(addr);
	if (hi != myMap.end())
	{
	    auto lo = --hi;
	    if (lo != myMap.end())
	    {
		return (hi->first - addr) <= (addr - lo->first) ?
		    &hi->second : &lo->second;
	    }
	    return &hi->second;
	}

	return 0;
    }

private:
    std::map<uint64, T>  myMap;
    QMutex		 myLock;
};

typedef SparseMap<std::string> StackTraceMap;

#endif
