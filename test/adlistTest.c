#include "test.h"
#include "xmadlist.h"

#include <stdio.h>

void printlist(list *l)
{
    listIter *iter;
    iter = listGetIterator(l, AL_START_HEAD);
    listNode *node;
    while ((node = listNext(iter)) != NULL)
    {
        char *s = listNodeValue(node);
        printf("%s  ", s);
    }
    printf("\n");
}

int main()
{
    list *l;
    l = listCreate();
    listAddNodeHead(l, "1");
    printlist(l);
    test_cond("new list", listLength(l) == 1 && listFirst(l) == listLast(l));

    listAddNodeTail(l, "2");
    printlist(l);
    test_cond("new list", listLength(l) == 2);

    listAddNodeHead(l, "0");
    printlist(l);

    list *ld;
    ld = listDup(l);
    printlist(ld);

    listRotate(ld);
    printlist(ld);

    listNode *node;
    node = listSearchKey(l, "1");
    listDelNode(l, node);
    printlist(l);

    node = listIndex(l, 0);
    listDelNode(l, node);
    printlist(l);
}