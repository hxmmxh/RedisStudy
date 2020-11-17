#include "test.h"
#include "xmsds.h"

#include <string.h>

int main()
{
    sdshdr *sh;
    sds x = sdsnew("foo"), y;

    test_cond("Create a string and obtain the length",
              sdslen(x) == 3 && memcmp(x, "foo\0", 4) == 0);

    sdsfree(x);
    x = sdsnewlen("foo", 2);
    test_cond("Create a string with specified length",
              sdslen(x) == 2 && memcmp(x, "fo\0", 3) == 0);

    x = sdscat(x, "bar");
    test_cond("Strings concatenation",
              sdslen(x) == 5 && memcmp(x, "fobar\0", 6) == 0);

    x = sdscpy(x, "a");
    test_cond("sdscpy() against an originally longer string",
              sdslen(x) == 1 && memcmp(x, "a\0", 2) == 0);

    x = sdscpy(x, "xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
              sdslen(x) == 33 &&
                  memcmp(x, "xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0", 34) == 0);
}