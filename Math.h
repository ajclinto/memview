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

#ifndef Math_H
#define Math_H

#include <stdio.h>
#include <limits.h>
#include <limits>
#include <string>

#define HAS_LAMBDA (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5))

typedef unsigned char		uint8;
typedef unsigned short		uint16;
typedef unsigned		uint32;
typedef unsigned long long	uint64;
typedef signed long long	int64;

template <typename T>
inline T SYSmax(T a, T b)
{
    return a > b ? a : b;
}
template <typename T>
inline T SYSmin(T a, T b)
{
    return a < b ? a : b;
}
template <typename T>
inline T SYSclamp(T v, T a, T b)
{
    return v < a ? a : (v > b ? b : v);
}
inline int SYSclamp32(int64 val)
{
    return (int)SYSclamp(val,
	    (int64)std::numeric_limits<int>::min(),
	    (int64)std::numeric_limits<int>::max());
}
inline int SYSclamp32(uint64 val)
{
    return (int)SYSmin(val, (uint64)std::numeric_limits<int>::max());
}
template <typename T>
inline T SYSlerp(T v1, T v2, T bias)
{
    return v1 + bias*(v2-v1);
}
template <typename T>
inline void SYSswap(T &v1, T &v2)
{
    T	tmp(v1);
    v1 = v2;
    v2 = tmp;
}
template <typename T>
inline T SYSabs(T a)
{
    return a >= 0 ? a : -a;
}

template <typename T>
struct Box {
    void initBounds()
    {
	T minv = std::numeric_limits<T>::min();
	T maxv = std::numeric_limits<T>::max();
	l[0] = maxv; h[0] = minv;
	l[1] = maxv; h[1] = minv;
    }
    void initBounds(T xl, T yl, T xh, T yh)
    {
	l[0] = xl; h[0] = xh;
	l[1] = yl; h[1] = yh;
    }
    void enlargeBounds(T xl, T yl, T xh, T yh)
    {
	l[0] = SYSmin(l[0], xl); h[0] = SYSmax(h[0], xh);
	l[1] = SYSmin(l[1], yl); h[1] = SYSmax(h[1], yh);
    }

    bool intersect(const Box &other)
    {
	for (T i = 0; i < 2; i++)
	{
	    l[i] = SYSmax(l[i], other.l[i]);
	    h[i] = SYSmin(h[i], other.h[i]);
	}
	return isValid();
    }
    bool isValid() const { return h[0] > l[0] && h[1] > l[1]; }

    void dump() const
    {
	fprintf(stderr, "box: %lld %lld - %lld %lld\n",
		(int64)l[0], (int64)l[1], (int64)h[0], (int64)h[1]);
    }

    T xmin() const { return l[0]; }
    T xmax() const { return h[0]; }

    T ymin() const { return l[1]; }
    T ymax() const { return h[1]; }

    T width() const { return h[0] - l[0]; }
    T height() const { return h[1] - l[1]; }

    T	l[2];
    T	h[2];
};

#if HAS_LAMBDA
inline std::string SYStoString(int val)
{
    return std::to_string(val);
}
#else
inline std::string SYStoString(int val)
{
    std::ostringstream os;
    os << val;
    return os.str();
}
#endif

#endif
