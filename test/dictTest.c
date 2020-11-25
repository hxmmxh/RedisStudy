#include "test.h"
#include "xmdict.h"
#include "xmmalloc.h"

#include <stdio.h>

void scan(void *p, const dictEntry *node)
{
    printf("%s-%d ", (char *)dictGetKey(node), (int)dictGetVal(node));
}

unsigned int hf(const void *key)
{
    return dictGenHashFunction(key, 0xffffffff);
}

int main()
{
    dict *d;
    dictType *func;
    func = (dictType *)xm_malloc(sizeof(*func));
    func->hashFunction = hf;

    d = dictCreate(func, NULL);
    unsigned int seed = dictGetHashFunctionSeed();
    test_cond("Hash seed", seed == 5381);

    dictAdd(d, "a", 1);
    test_cond("add 1 key", dictSlots(d) == 4 && dictSize(d) == 1 && !dictIsRehashing(d));
    dictAdd(d, "b", 2);
    test_cond("add 2 keys ", dictSlots(d) == 4 && dictSize(d) == 2 && !dictIsRehashing(d)&& dictFetchValue(d, "b") == 2); 
    dictReplace(d, "b", 0);
    test_cond("replace 1 key ", dictSlots(d) == 4 && dictSize(d) == 2 && !dictIsRehashing(d)&& dictFetchValue(d, "b") == 0);
    dictReplace(d, "c", 3);
    test_cond("replace a new key ", dictSlots(d) == 4 && dictSize(d) == 3);
    dictAdd(d, "d", 4);
    test_cond("4 keys", dictSlots(d) == 4 && dictSize(d) == 4);
    dictAdd(d, "e", 5);
    test_cond("5 keys", dictSlots(d) == 12 && dictSize(d) == 5 && dictIsRehashing(d));
    dictAdd(d, "f", 6);
    dictAdd(d, "g", 7);
    dictAdd(d, "h", 8);
    test_cond("8 keys", dictSlots(d) == 8 && dictSize(d) == 8);

    dictRehash(d, 10);
    test_cond("after rehash", dictSlots(d) == 8 && dictSize(d) == 8 && !dictIsRehashing(d));

    dictResize(d);
    test_cond("resize", dictSlots(d) == 16 && dictSize(d) == 8 && dictIsRehashing(d));

    dictIterator *iter;
    dictEntry *node;
    iter = dictGetIterator(d);
    while (node = dictNext(iter))
        printf("%s-%d ", dictGetKey(node), dictGetVal(node));
    printf("\n");
    dictReleaseIterator(iter);

    unsigned long v = 0;
    do
    {
        v = dictScan(d, v, scan, NULL);
    } while (v != 0);
    printf("\n");

    for (int i = 0; i < 10; ++i)
    {
        node = dictGetRandomKey(d);
        printf("%s-%d ", dictGetKey(node), dictGetVal(node));
    }
    printf("\n");

    test_report();
}