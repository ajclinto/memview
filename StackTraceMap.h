#ifndef StackTraceMap_H
#define StackTraceMap_H

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
		myMap[addr] = stack;
	    }

private:
    std::map<uint64, std::string>  myMap;
};

#endif
