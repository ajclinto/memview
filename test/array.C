#include "../SparseArray.h"

bool
testBasic()
{
    SparseArray<int, 26, 12> arr(34);
    unsigned long long       val = 17175675094ull;

    arr.setExists(val);
    return true;
}

int
main()
{
    bool ok = true;

    ok &= testBasic();

    return ok ? 0 : 1;
}
