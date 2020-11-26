#include "test.h"
#include "xmobject.h"
#include "xmsds.h"
#include "xmmalloc.h"
#include "xmskiplist.h"

int main()
{
    zskiplist *zsl;
    zsl = zslCreate();

    robj *no[10];
    for (int i = 0; i < 10; ++i)
    {
        no[i] = createStringObject('a' + i, 1);
    }

    for (int i = 0; i < 10; ++i)
    {
        zslInsert(zsl, 10 - i, no[i]);
    }

    for (int i = 0; i < 10; ++i)
    {
        zskiplistNode *node;
        node = zslGetElementByRank(zsl, i + 1);
        robj *o = node->obj;
        printf("%s ", o->ptr);
    }

    zslFree(zsl);
}