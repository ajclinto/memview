#include "../IntervalMap.h"

#define FIND(IDX, STR) \
    if (map.find(IDX) != STR) \
    { \
	fprintf(stderr, "find: %d %s\n", IDX, STR); \
	return 1; \
    }

#define CLOSEST(IDX, STR) \
    if (map.findClosest(IDX) != STR) \
    { \
	fprintf(stderr, "findClosest: %d %s\n", IDX, STR); \
	return 1; \
    }

int main()
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
	return 1;
    }

    FIND(15, "")
    CLOSEST(15, "")

    return 0;
}
