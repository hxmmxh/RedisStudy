
#include "xmobject.h"
#include "xmmalloc.h"
#include "xmsds.h"

#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

// 返回微秒格式的 UNIX 时间
// 1 秒 = 1 000 000 微秒
static long long ustime(void)
{
    struct timeval tv;
    long long ust;
    // 获取当前时间，UTC时间，精度微妙，无时区转换
    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

// 返回毫秒格式的 UNIX 时间
// 1 秒 = 1 000 毫秒
static long long mstime(void)
{
    return ustime() / 1000;
}

// 返回秒格式的UNIX时间
unsigned int getLRUClock(void)
{
    return (mstime() / REDIS_LRU_CLOCK_RESOLUTION) & REDIS_LRU_CLOCK_MAX;
}

void createSharedObjects(void)
{
    int j;
    // 常用整数
    for (j = 0; j < REDIS_SHARED_INTEGERS; j++)
    {
        shared.integers[j] = createObject(REDIS_STRING, (void *)(long)j);
        shared.integers[j]->encoding = REDIS_ENCODING_INT;
    }
}

robj *createObject(int type, void *ptr)
{
    robj *o = xm_malloc(sizeof(*o));
    o->type = type;
    o->encoding = REDIS_ENCODING_RAW;
    o->ptr = ptr;
    o->refcount = 1;
    o->lru = LRU_CLOCK();
    return o;
}

void incrRefCount(robj *o)
{
    o->refcount++;
}

void decrRefCount(robj *o)
{
    if (o->refcount <= 0)
        printf("decrRefCount against refcount <= 0");

    // 释放对象
    if (o->refcount == 1)
    {
        switch (o->type)
        {
        case REDIS_STRING:
            freeStringObject(o);
            break;
            /*case REDIS_LIST:
            freeListObject(o);
            break;
        case REDIS_SET:
            freeSetObject(o);
            break;
        case REDIS_ZSET:
            freeZsetObject(o);
            break;
        case REDIS_HASH:
            freeHashObject(o);
            break;
        */

        default:
            printf("Unknown object type");
            break;
        }
        xm_free(o);
    }
    else
    {
        o->refcount--;
    }
}

void decrRefCountVoid(void *o)
{
    decrRefCount(o);
}

/*  functionThatWillIncrementRefCount(resetRefCount(CreateObject(...)));
 *
 * Otherwise you need to resort to the less elegant pattern:
 *
 * 没有这个函数的话，事情就会比较麻烦了：
 *
 *    *obj = createObject(...);
 *    functionThatWillIncrementRefCount(obj);
 *    decrRefCount(obj);
 */
robj *resetRefCount(robj *obj)
{
    obj->refcount = 0;
    return obj;
}

int checkType(/*redisClient *c, */ robj *o, int type)
{
    if (o->type != type)
    {
        // addReply(c, shared.wrongtypeerr);
        return 1;
    }
    return 0;
}

char *strEncoding(int encoding)
{
    switch (encoding)
    {
    case REDIS_ENCODING_RAW:
        return "raw";
    case REDIS_ENCODING_INT:
        return "int";
    case REDIS_ENCODING_HT:
        return "hashtable";
    case REDIS_ENCODING_LINKEDLIST:
        return "linkedlist";
    case REDIS_ENCODING_ZIPLIST:
        return "ziplist";
    case REDIS_ENCODING_INTSET:
        return "intset";
    case REDIS_ENCODING_SKIPLIST:
        return "skiplist";
    case REDIS_ENCODING_EMBSTR:
        return "embstr";
    default:
        return "unknown";
    }
}

unsigned long long estimateObjectIdleTime(robj *o)
{
    unsigned long long lruclock = LRU_CLOCK();
    if (lruclock >= o->lru)
    {
        return (lruclock - o->lru) * REDIS_LRU_CLOCK_RESOLUTION;
    }
    // 说明溢出了
    else
    {
        return (lruclock + (REDIS_LRU_CLOCK_MAX - o->lru)) *
               REDIS_LRU_CLOCK_RESOLUTION;
    }
}
