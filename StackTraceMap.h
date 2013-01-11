#ifndef StackTraceMap_H
#define StackTraceMap_H

#include <QMutex>
#include "Math.h"
#include <unordered_set>
#include <map>
#include <string>

class StackTraceMap {
public:
     StackTraceMap();
    ~StackTraceMap();

    void    insert(uint64 addr, const char *stack)
    {
	QMutexLocker lock(&myLock);
	myMap[addr] = stack;
    }

    const char	*findClosestStackTrace(uint64 addr)
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
		return SYSabs((int64)addr - (int64)hi->first) <
		       SYSabs((int64)addr - (int64)lo->first) ?
		       hi->second.c_str() :
		       lo->second.c_str();
	    }
	    return hi->second.c_str();
	}

	return 0;
    }

private:
    std::map<uint64, std::string>  myMap;
    QMutex			   myLock;
};

#endif
