
#include "xmobject.h"
#include "xmmalloc.h"

#include <math.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

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

// 共享对象
struct sharedObjects shared;

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

robj *createRawStringObject(char *ptr, size_t len)
{
    return createObject(REDIS_STRING, sdsnewlen(ptr, len));
}

robj *createEmbeddedStringObject(char *ptr, size_t len)
{
    //一起分配robj和sds的空间
    robj *o = zmalloc(sizeof(robj) + sizeof(struct sdshdr) + len + 1);
    //o+1会让o启动robj大小的距离
    struct sdshdr *sh = (void *)(o + 1);

    o->type = REDIS_STRING;
    o->encoding = REDIS_ENCODING_EMBSTR;
    // sh+1指向了buf，保持和sds一致
    o->ptr = sh + 1;
    o->refcount = 1;
    o->lru = LRU_CLOCK();

    sh->len = len;
    sh->free = 0;
    if (ptr)
    {
        memcpy(sh->buf, ptr, len);
        sh->buf[len] = '\0';
    }
    else
    {
        memset(sh->buf, 0, len + 1);
    }
    return o;
}

#define REDIS_ENCODING_EMBSTR_SIZE_LIMIT 39
robj *createStringObject(char *ptr, size_t len)
{
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT)
        return createEmbeddedStringObject(ptr, len);
    else
        return createRawStringObject(ptr, len);
}

robj *createStringObjectFromLongLong(long long value)
{
    robj *o;
    // 如果value 的大小符合 REDIS 共享整数的范围，返回一个共享对象
    if (value >= 0 && value < REDIS_SHARED_INTEGERS)
    {
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];
    }
    // 不符合共享范围，创建一个新的整数对象
    else
    {
        // 如果值可以用 long 类型保存，创建一个 REDIS_ENCODING_INT 编码的字符串对象
        if (value >= LONG_MIN && value <= LONG_MAX)
        {
            o = createObject(REDIS_STRING, NULL);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void *)((long)value);
        }
        // 如果不能用 long 类型保存（long long 类型），则将值转换为字符串，
        // 并创建一个 REDIS_ENCODING_RAW 的字符串对象来保存值
        else
        {
            o = createObject(REDIS_STRING, sdsfromlonglong(value));
        }
    }
    return o;
}

robj *createStringObjectFromLongDouble(long double value)
{
    char buf[256];
    int len;
    // 使用 17 位小数精度，这种精度可以在大部分机器上被 rounding 而不改变
    len = snprintf(buf, sizeof(buf), "%.17Lf", value);
    // 移除尾部的 0
    // 比如 3.1400000 将变成 3.14
    // 而 3.00000 将变成 3
    // strchr搜索第一次出现指定字符的位置，返回char*
    if (strchr(buf, '.') != NULL)
    {
        char *p = buf + len - 1;
        while (*p == '0')
        {
            p--;
            len--;
        }
        // 如果不需要小数点，那么移除它
        if (*p == '.')
            len--;
    }
    // 创建对象
    return createStringObject(buf, len);
}

robj *dupStringObject(robj *o)
{
    robj *d;
    switch (o->encoding)
    {
    case REDIS_ENCODING_RAW:
        return createRawStringObject(o->ptr, sdslen(o->ptr));
    case REDIS_ENCODING_EMBSTR:
        return createEmbeddedStringObject(o->ptr, sdslen(o->ptr));
    //在复制一个包含整数值的字符串对象时，总是产生一个非共享的对象
    case REDIS_ENCODING_INT:
        d = createObject(REDIS_STRING, NULL);
        d->encoding = REDIS_ENCODING_INT;
        d->ptr = o->ptr;
        return d;
    default:
        printf("Wrong encoding.");
        break;
    }
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
        case REDIS_LIST:
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