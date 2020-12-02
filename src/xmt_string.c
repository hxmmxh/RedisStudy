
#include "xmt_string.h"
#include "xmmalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

robj *createRawStringObject(char *ptr, size_t len)
{
    return createObject(REDIS_STRING, sdsnewlen(ptr, len));
}

robj *createEmbeddedStringObject(char *ptr, size_t len)
{
    //一起分配robj和sds的空间
    robj *o = xm_malloc(sizeof(robj) + sizeof(struct sdshdr) + len + 1);
    //o+1会让o前进robj大小的距离
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
    return NULL;
}

void freeStringObject(robj *o)
{
    // 只有raw编码需要去单独是否sds的空间
    // embstr的空间是和object一起分配的
    if (o->encoding == REDIS_ENCODING_RAW)
    {
        sdsfree(o->ptr);
    }
}

int isObjectRepresentableAsLongLong(robj *o, long long *llval)
{
    assert(o->type == REDIS_STRING);
    // INT 编码的 long 值总是能保存为 long long
    if (o->encoding == REDIS_ENCODING_INT)
    {
        if (llval)
            *llval = (long)o->ptr;
        return REDIS_OK;
    }
    // 如果是字符串的话，那么尝试将它转换为 long long
    else
    {
        return string2ll(o->ptr, sdslen(o->ptr), llval) ? REDIS_OK : REDIS_ERR;
    }
}

robj *tryObjectEncoding(robj *o)
{
    long value;

    sds s = o->ptr;
    size_t len;

    assert(o->type == REDIS_STRING);

    // 只在字符串的编码为 RAW 或者 EMBSTR 时尝试进行编码
    if (!sdsEncodedObject(o))
        return o;

    // 不对共享对象进行编码
    if (o->refcount > 1)
        return o;

    // 只对长度小于或等于 21 字节，并且可以被解释为整数的字符串进行编码
    len = sdslen(s);
    if (len <= 21 && string2l(s, len, &value))
    {
        // 如果服务器打开了 maxmemory 选项.
        // 那么当服务器占用的内存数超过了 maxmemory 选项所设置的上限值时，空转时长较高的那部分键会优先被服务器释放， 从而回收内存
        // 这个时候最好不使用共享对象，因为每个对象都要有自己私有的LRU时间
        if (/*server.maxmemory == 0 &&*/
            value >= 0 &&
            value < REDIS_SHARED_INTEGERS)
        {
            decrRefCount(o);
            incrRefCount(shared.integers[value]);
            return shared.integers[value];
        }
        else
        {
            if (o->encoding == REDIS_ENCODING_RAW)
                sdsfree(o->ptr);
            o->encoding = REDIS_ENCODING_INT;
            o->ptr = (void *)value;
            return o;
        }
    }

    // 尝试将 RAW 编码的字符串编码为 EMBSTR 编码
    if (len <= REDIS_ENCODING_EMBSTR_SIZE_LIMIT)
    {
        robj *emb;

        if (o->encoding == REDIS_ENCODING_EMBSTR)
            return o;
        emb = createEmbeddedStringObject(s, sdslen(s));
        decrRefCount(o);
        return emb;
    }

    // 如果这个对象没办法进行编码，尝试从 SDS 中移除空余空间
    if (o->encoding == REDIS_ENCODING_RAW &&
        sdsavail(s) > len / 10)
    {
        o->ptr = sdsRemoveFreeSpace(o->ptr);
    }

    return o;
}

robj *getDecodedObject(robj *o)
{
    robj *dec;

    if (sdsEncodedObject(o))
    {
        // 如果对象已经是字符串编码的，那么对输入对象的引用计数增一
        incrRefCount(o);
        return o;
    }

    // 解码对象，将对象的值从整数转换为字符串
    if (o->type == REDIS_STRING && o->encoding == REDIS_ENCODING_INT)
    {
        char buf[32];

        ll2string(buf, 32, (long)o->ptr);
        dec = createStringObject(buf, strlen(buf));
        return dec;
    }
    else
    {
        printf("Unknown encoding type");
    }
}

/*
strcoll()会依环境变量LC_COLLATE所指定的文字排列次序来比较s1和s2 字符串。
strcmp是根据ASCII来比较2个串的.
当LC_COLLATE为"POSIX"或"C"，strcoll()与strcmp()作用完全相同
*/
#define REDIS_COMPARE_BINARY (1 << 0)
#define REDIS_COMPARE_COLL (1 << 1)
// 比较两个字符串对象的大小
static int compareStringObjectsWithFlags(robj *a, robj *b, int flags)
{
    char bufa[128], bufb[128], *astr, *bstr;
    size_t alen, blen, minlen;
    if (a == b)
        return 0;
    // 指向字符串值
    if (sdsEncodedObject(a))
    {
        astr = a->ptr;
        alen = sdslen(astr);
    }
    // 保存的是整数值，将整数转换为字符串
    else
    {
        alen = ll2string(bufa, sizeof(bufa), (long)a->ptr);
        astr = bufa;
    }

    // 同样处理字符串 b
    if (sdsEncodedObject(b))
    {
        bstr = b->ptr;
        blen = sdslen(bstr);
    }
    else
    {
        blen = ll2string(bufb, sizeof(bufb), (long)b->ptr);
        bstr = bufb;
    }
    
    if (flags & REDIS_COMPARE_COLL)
    {
        return strcoll(astr, bstr);
    }
    else
    {
        int cmp;
        minlen = (alen < blen) ? alen : blen;
        cmp = memcmp(astr, bstr, minlen);
        if (cmp == 0)
            return alen - blen;
        return cmp;
    }
}

int compareStringObjects(robj *a, robj *b)
{
    return compareStringObjectsWithFlags(a, b, REDIS_COMPARE_BINARY);
}

int collateStringObjects(robj *a, robj *b)
{
    return compareStringObjectsWithFlags(a, b, REDIS_COMPARE_COLL);
}

int equalStringObjects(robj *a, robj *b)
{

    // 对象的编码为 INT ，直接对比值
    // 这里避免了将整数值转换为字符串，所以效率更高
    if (a->encoding == REDIS_ENCODING_INT &&
        b->encoding == REDIS_ENCODING_INT)
    {
        return a->ptr == b->ptr;
    }
    // 进行字符串对象
    else
    {
        return compareStringObjects(a, b) == 0;
    }
}

size_t stringObjectLen(robj *o)
{
    if (sdsEncodedObject(o))
    {
        return sdslen(o->ptr);
    }
    // INT 编码，计算将这个值转换为字符串要多少字节,相当于返回它的长度
    else
    {
        char buf[32];
        return ll2string(buf, 32, (long)o->ptr);
    }
}

int getDoubleFromObject(robj *o, double *target)
{
    double value;
    char *eptr;

    if (o == NULL)
    {
        value = 0;
    }
    else
    {
        // 尝试从字符串中转换 double 值
        if (sdsEncodedObject(o))
        {
            errno = 0;
            // double strtod(const char *str, char **endptr)
            // 把参数 str 所指向的字符串转换为一个浮点数（类型为 double 型）。
            // 如果 endptr 不为空，则指向转换中最后一个字符后的字符的指针会存储在 endptr 引用的位置
            value = strtod(o->ptr, &eptr);
            if (isspace(((char *)o->ptr)[0]) ||
                eptr[0] != '\0' ||
                (errno == ERANGE &&
                 (value == HUGE_VAL || value == -HUGE_VAL || value == 0)) ||
                errno == EINVAL ||
                isnan(value))
                return REDIS_ERR;
        }
        // INT 编码
        else if (o->encoding == REDIS_ENCODING_INT)
        {
            value = (long)o->ptr;
        }
        else
        {
            //redisPanic("Unknown string encoding");
        }
    }

    // 返回值
    *target = value;
    return REDIS_OK;
}

int getDoubleFromObjectOrReply(/*redisClient *c,*/ robj *o, double *target, const char *msg)
{

    double value;
    /*
    if (getDoubleFromObject(o, &value) != REDIS_OK)
    {
        if (msg != NULL)
        {
            addReplyError(c, (char *)msg);
        }
        else
        {
            addReplyError(c, "value is not a valid float");
        }
        return REDIS_ERR;
    }
    target = value;
    */
    return REDIS_OK;
}

int getLongDoubleFromObject(robj *o, long double *target)
{
    long double value;
    char *eptr;

    if (o == NULL)
    {
        value = 0;
    }
    else
    {

        // RAW 编码，尝试从字符串中转换 long double
        if (sdsEncodedObject(o))
        {
            errno = 0;
            value = strtold(o->ptr, &eptr);
            if (isspace(((char *)o->ptr)[0]) || eptr[0] != '\0' ||
                errno == ERANGE || isnan(value))
                return REDIS_ERR;
        }
        // INT 编码，直接保存
        else if (o->encoding == REDIS_ENCODING_INT)
        {
            value = (long)o->ptr;
        }
        else
        {
            //redisPanic("Unknown string encoding");
        }
    }
    *target = value;
    return REDIS_OK;
}

int getLongDoubleFromObjectOrReply(/*redisClient *c,*/ robj *o, long double *target, const char *msg)
{
    /*
    long double value;
    
    if (getLongDoubleFromObject(o, &value) != REDIS_OK)
    {
        if (msg != NULL)
        {
            addReplyError(c, (char *)msg);
        }
        else
        {
            addReplyError(c, "value is not a valid float");
        }
        return REDIS_ERR;
    }

    *target = value;
    */
    return REDIS_OK;
}

int getLongLongFromObject(robj *o, long long *target)
{
    long long value;
    char *eptr;

    if (o == NULL)
    {
        // o 为 NULL 时，将值设为 0 。
        value = 0;
    }
    else
    {
        if (sdsEncodedObject(o))
        {
            errno = 0;
            // T = O(N)
            value = strtoll(o->ptr, &eptr, 10);
            if (isspace(((char *)o->ptr)[0]) || eptr[0] != '\0' ||
                errno == ERANGE)
                return REDIS_ERR;
        }
        else if (o->encoding == REDIS_ENCODING_INT)
        {
            // 对于 REDIS_ENCODING_INT 编码的整数值
            // 直接将它的值保存到 value 中
            value = (long)o->ptr;
        }
        else
        {
            //redisPanic("Unknown string encoding");
        }
    }

    // 保存值到指针
    if (target)
        *target = value;

    // 返回结果标识符
    return REDIS_OK;
}

int getLongLongFromObjectOrReply(/*redisClient *c, */ robj *o, long long *target, const char *msg)
{
    /*
    long long value;

    // T = O(N)
    if (getLongLongFromObject(o, &value) != REDIS_OK)
    {
        if (msg != NULL)
        {
            addReplyError(c, (char *)msg);
        }
        else
        {
            addReplyError(c, "value is not an integer or out of range");
        }
        return REDIS_ERR;
    }

    *target = value;
    */
    return REDIS_OK;
}

int getLongFromObjectOrReply(/*redisClient *c,*/ robj *o, long *target, const char *msg)
{
    long long value;

    // 先尝试以 long long 类型取出值
    if (getLongLongFromObjectOrReply(/*c,*/ o, &value, msg) != REDIS_OK)
        return REDIS_ERR;

    // 然后检查值是否在 long 类型的范围之内
    if (value < LONG_MIN || value > LONG_MAX)
    {
        if (msg != NULL)
        {
            //addReplyError(c, (char *)msg);
        }
        else
        {
            //addReplyError(c, "value is out of range");
        }
        return REDIS_ERR;
    }

    *target = value;
    return REDIS_OK;
}