#include "StackTraceMap.h"

StackTraceMap::StackTraceMap()
{
}

StackTraceMap::~StackTraceMap()
{
    fprintf(stderr, "%d traces\n", (int)myMap.size());
}
