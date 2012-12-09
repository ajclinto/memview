#ifndef Math_H
#define Math_H

#include <stdio.h>

typedef unsigned short		uint16;
typedef unsigned		uint32;
typedef unsigned long long	uint64;

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

struct Box {
    void	initBounds(int xl, int yl, int xh, int yh)
    {
	l[0] = xl; h[0] = xh;
	l[1] = yl; h[1] = yh;
    }

    bool	intersect(const Box &other)
    {
	for (int i = 0; i < 2; i++)
	{
	    l[i] = SYSmax(l[i], other.l[i]);
	    h[i] = SYSmin(h[i], other.h[i]);
	}
	return isValid();
    }
    bool	isValid() const { return h[0] > l[0] && h[1] > l[1]; }

    void	dump() const
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
