#include "../IntervalMap.h"

#define FIND(IDX, STR) \
    if (map.find(IDX) != STR) \
    { \
	fprintf(stderr, "find: %d %s\n", IDX, STR); \
	return false; \
    }

#define CLOSEST(IDX, STR) \
    if (map.findClosest(IDX) != STR) \
    { \
	fprintf(stderr, "findClosest: %d %s\n", IDX, STR); \
	return false; \
    }

bool
testBasic()
{
    StackTraceMap   map;

    map.insert(1, 2, "test1");
    map.insert(10, 20, "test2");

    FIND(0, "")
    FIND(1, "test1")
    FIND(2, "")
    FIND(15, "test2")
    FIND(20, "")
    FIND(100, "")

    CLOSEST(0, "test1")
    CLOSEST(1, "test1")
    CLOSEST(8, "test2")
    CLOSEST(100, "test2")

    map.erase(1, 2);

    FIND(1, "")

    map.erase(10, 20);

    if (map.size())
    {
	fprintf(stderr, "map should be empty\n");
	return false;
    }

    FIND(15, "")
    CLOSEST(15, "")

    return true;
}

bool
testOverlap()
{
    StackTraceMap   map;

    map.insert(0, 10, "test1");
    map.insert(5, 15, "test2");
    map.insert(10, 12, "test3");

    fprintf(stderr, "Overlapping intervals:\n");
    map.dump();

    return true;
}

int
main()
{
    bool    ok = true;

    ok &= testBasic();
    ok &= testOverlap();

    return ok ? 0 : 1;
}
