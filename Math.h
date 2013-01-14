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

struct Box {
    void initBounds()
    {
	l[0] = INT_MAX; h[0] = -INT_MAX;
	l[1] = INT_MAX; h[1] = -INT_MAX;
    }
    void initBounds(int xl, int yl, int xh, int yh)
    {
	l[0] = xl; h[0] = xh;
	l[1] = yl; h[1] = yh;
    }
    void enlargeBounds(int xl, int yl, int xh, int yh)
    {
	l[0] = SYSmin(l[0], xl); h[0] = SYSmax(h[0], xh);
	l[1] = SYSmin(l[1], yl); h[1] = SYSmax(h[1], yh);
    }

    bool intersect(const Box &other)
    {
	for (int i = 0; i < 2; i++)
	{
	    l[i] = SYSmax(l[i], other.l[i]);
	    h[i] = SYSmin(h[i], other.h[i]);
	}
	return isValid();
    }
    bool isValid() const { return h[0] > l[0] && h[1] > l[1]; }

    void dump() const
    {
	fprintf(stderr, "box: %d %d - %d %d\n", l[0], l[1], h[0], h[1]);
    }

    int xmin() const { return l[0]; }
    int xmax() const { return h[0]; }

    int ymin() const { return l[1]; }
    int ymax() const { return h[1]; }

    int width() const { return h[0] - l[0]; }
    int height() const { return h[1] - l[1]; }

    int	l[2];
    int	h[2];
};

#endif
