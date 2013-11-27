#include "../IntervalMap.h"

#define FIND(METHOD, IDX, STR) \
    { \
	auto it = writer.METHOD(IDX); \
	if ((it != writer.end() ? it.value() : "") != STR) \
	{ \
	    fprintf(stderr, #METHOD ": %d %s\n", IDX, STR); \
	    return false; \
	} \
    }

typedef IntervalMap<std::string> StringMap;
typedef IntervalMapReader<std::string> StringMapReader;
typedef IntervalMapWriter<std::string> StringMapWriter;

bool
testBasic()
{
    StringMap   map;
    StringMapWriter	writer(map);

    writer.insert(1, 2, "test1");
    writer.insert(10, 20, "test2");

    FIND(find, 0, "")
    FIND(find, 1, "test1")
    FIND(find, 2, "")
    FIND(find, 15, "test2")
    FIND(find, 20, "")
    FIND(find, 100, "")

    FIND(findClosest, 0, "test1")
    FIND(findClosest, 1, "test1")
    FIND(findClosest, 8, "test2")
    FIND(findClosest, 100, "test2")

    writer.erase(1, 2);

    FIND(find, 1, "")

    writer.erase(10, 20);

    if (writer.size())
    {
	fprintf(stderr, "map should be empty\n");
	return false;
    }

    FIND(find, 15, "")
    FIND(findClosest, 15, "")

    return true;
}

bool
testOverlap()
{
    StringMap   map;
    StringMapWriter	writer(map);

    writer.insert(0, 10, "test1");
    writer.insert(5, 15, "test2");
    writer.insert(10, 12, "test3");

    fprintf(stderr, "Overlapping intervals:\n");
    writer.dump();

    writer.apply(0, 15, [](std::string &str) { str = ""; });

    fprintf(stderr, "After apply:\n");
    writer.dump();

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
