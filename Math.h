#ifndef Math_H
#define Math_H

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

#endif
