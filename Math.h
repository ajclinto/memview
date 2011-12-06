#ifndef Math_H
#define Math_H

typedef unsigned short		uint16;
typedef unsigned		uint32;
typedef unsigned long long	uint64;

inline uint32 SYSmax(uint32 a, uint32 b)
{
    return a > b ? a : b;
}
inline uint32 SYSmin(uint32 a, uint32 b)
{
    return a < b ? a : b;
}
inline uint32 SYSclamp(uint32 v, uint32 a, uint32 b)
{
    return v < a ? a : (v > b ? b : v);
}
inline float SYSlerp(float v1, float v2, float bias)
{
    return v1 + bias*(v2-v1);
}

#endif
