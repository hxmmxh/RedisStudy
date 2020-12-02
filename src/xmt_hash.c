#include "xmt_hash.h"

/***********字典的特定函数************************************************/

unsigned int dictEncObjHash(const void *key)
{
    robj *o = (robj *)key;

    // 如果是字符串编码
    if (sdsEncodedObject(o))
    {
        return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
    }
    else
    {
        if (o->encoding == REDIS_ENCODING_INT)
        {
            char buf[32];
            int len;
            // 先把整数转换成字符串
            len = ll2string(buf, 32, (long)o->ptr);
            return dictGenHashFunction((unsigned char *)buf, len);
        }
        // 不知道什么情况会来这个分支？？？
        else
        {
            unsigned int hash;

            o = getDecodedObject(o);
            hash = dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
            decrRefCount(o);
            return hash;
        }
    }
}

int dictEncObjKeyCompare(void *privdata, const void *key1,
                         const void *key2)
{
    robj *o1 = (robj *)key1, *o2 = (robj *)key2;
    int cmp;

    if (o1->encoding == REDIS_ENCODING_INT &&
        o2->encoding == REDIS_ENCODING_INT)
        return o1->ptr == o2->ptr;

    o1 = getDecodedObject(o1);
    o2 = getDecodedObject(o2);
    cmp = dictSdsKeyCompare(privdata, o1->ptr, o2->ptr);
    decrRefCount(o1);
    decrRefCount(o2);
    return cmp;
}

void dictRedisObjectDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    if (val == NULL)
        return; /* Values of swapped out keys as set to NULL */
    decrRefCount(val);
}

dictType hashDictType = {
    dictEncObjHash,            /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictEncObjKeyCompare,      /* key compare */
    dictRedisObjectDestructor, /* key destructor */
    dictRedisObjectDestructor  /* val destructor */
};

/******************************************************************/

robj *createHashObject(void)
{
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_HASH, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

void hashTypeTryConversion(robj *o, robj **argv, int start, int end)
{
    int i;

    // 如果对象不是 ziplist 编码，那么直接返回
    if (o->encoding != REDIS_ENCODING_ZIPLIST)
        return;

    // 检查所有输入对象，看它们的字符串值是否超过了指定长度
    for (i = start; i <= end; i++)
    {
        // 只检查字符串值，因为它们的长度可以在常数时间内取得。
        if (sdsEncodedObject(argv[i]) &&
            sdslen(argv[i]->ptr) > server.hash_max_ziplist_value)
        {
            // 将对象的编码转换成 REDIS_ENCODING_HT
            hashTypeConvert(o, REDIS_ENCODING_HT);
            break;
        }
    }
}

// 将一个 ziplist 编码的哈希对象 o 转换成其他编码
void hashTypeConvertZiplist(robj *o, int enc)
{

    // 如果输入是 ZIPLIST ，那么不做动作
    if (enc == REDIS_ENCODING_ZIPLIST)
    {
        /* Nothing to do... */
    }
    else if (enc == REDIS_ENCODING_HT)
    {
        hashTypeIterator *hi;
        dict *dict;
        int ret;
        // 创建哈希迭代器
        hi = hashTypeInitIterator(o);
        // 创建空白的新字典
        dict = dictCreate(&hashDictType, NULL);

        // 遍历整个 ziplist
        while (hashTypeNext(hi) != REDIS_ERR)
        {
            robj *field, *value;

            // 取出 ziplist 里的键
            field = hashTypeCurrentObject(hi, REDIS_HASH_KEY);
            field = tryObjectEncoding(field);

            // 取出 ziplist 里的值
            value = hashTypeCurrentObject(hi, REDIS_HASH_VALUE);
            value = tryObjectEncoding(value);

            // 将键值对添加到字典
            ret = dictAdd(dict, field, value);
        }

        // 释放 ziplist 的迭代器
        hashTypeReleaseIterator(hi);

        // 释放对象原来的 ziplist
        xm_free(o->ptr);

        // 更新哈希的编码和值对象
        o->encoding = REDIS_ENCODING_HT;
        o->ptr = dict;
    }
    else
    {
        //redisPanic("Unknown hash encoding");
    }
}

void hashTypeConvert(robj *o, int enc)
{

    if (o->encoding == REDIS_ENCODING_ZIPLIST)
    {
        hashTypeConvertZiplist(o, enc);
    }
    else if (o->encoding == REDIS_ENCODING_HT)
    {
        //redisPanic("Not implemented");
    }
    else
    {
        //redisPanic("Unknown hash encoding");
    }
}

void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2)
{
    if (subject->encoding == REDIS_ENCODING_HT)
    {
        if (o1)
            *o1 = tryObjectEncoding(*o1);
        if (o2)
            *o2 = tryObjectEncoding(*o2);
    }
}

/* 从 ziplist 编码的 hash 中取出和 field 相对应的值。
 *
 * 参数：
 *  field   键
 *  vstr    值是字符串时，将它保存到这个指针
 *  vlen    保存字符串的长度
 *  ll      值是整数时，将它保存到这个指针
 *
 * 查找失败时，函数返回 -1 。
 * 查找成功时，返回 0 。
 */
int hashTypeGetFromZiplist(robj *o, robj *field,
                           unsigned char **vstr,
                           unsigned int *vlen,
                           long long *vll)
{
    unsigned char *zl, *fptr = NULL, *vptr = NULL;
    int ret;

    // 取出未编码的域
    field = getDecodedObject(field);

    // 遍历 ziplist ，查找域的位置
    zl = o->ptr;
    fptr = ziplistIndex(zl, ZIPLIST_HEAD);
    if (fptr != NULL)
    {
        // 定位包含域的节点，每次查找都要跳过一个节点，因为只查找键
        fptr = ziplistFind(fptr, field->ptr, sdslen(field->ptr), 1);
        if (fptr != NULL)
        {
            // 键已经找到，取出和它相对应的值的位置，就是它的后一个值
            vptr = ziplistNext(zl, fptr);
        }
    }
    decrRefCount(field);
    // 从 ziplist 节点中取出值
    if (vptr != NULL)
    {
        ret = ziplistGet(vptr, vstr, vlen, vll);
        return 0;
    }
    // 没找到
    return -1;
}

/* 从 REDIS_ENCODING_HT 编码的 hash 中取出和 field 相对应的值。
 * 成功找到值时返回 0 ，没找到返回 -1 。
 */
int hashTypeGetFromHashTable(robj *o, robj *field, robj **value)
{
    dictEntry *de;
    // 在字典中查找域（键）
    de = dictFind(o->ptr, field);
    // 键不存在
    if (de == NULL)
        return -1;
    // 取出域（键）的值
    *value = dictGetVal(de);
    // 成功找到
    return 0;
}

robj *hashTypeGetObject(robj *o, robj *field)
{
    robj *value = NULL;

    if (o->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0)
        {
            // 创建值对象
            if (vstr)
            {
                value = createStringObject((char *)vstr, vlen);
            }
            else
            {
                value = createStringObjectFromLongLong(vll);
            }
        }
    }
    else if (o->encoding == REDIS_ENCODING_HT)
    {
        robj *aux;

        if (hashTypeGetFromHashTable(o, field, &aux) == 0)
        {
            incrRefCount(aux);
            value = aux;
        }
    }
    else
    {
        // redisPanic("Unknown hash encoding");
    }

    // 返回值对象，或者 NULL
    return value;
}

int hashTypeExists(robj *o, robj *field)
{
    if (o->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0)
            return 1;
    }
    else if (o->encoding == REDIS_ENCODING_HT)
    {
        robj *aux;

        if (hashTypeGetFromHashTable(o, field, &aux) == 0)
            return 1;
    }
    else
    {
        //redisPanic("Unknown hash encoding");
    }

    // 不存在
    return 0;
}

int hashTypeSet(robj *o, robj *field, robj *value)
{
    int update = 0;

    if (o->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *zl, *fptr, *vptr;

        // 解码成字符串或者数字
        field = getDecodedObject(field);
        value = getDecodedObject(value);

        // 遍历整个 ziplist ，尝试查找并更新 field （如果它已经存在的话）
        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL)
        {
            fptr = ziplistFind(fptr, field->ptr, sdslen(field->ptr), 1);
            // 如果找到了field
            if (fptr != NULL)
            {
                // 定位到值
                vptr = ziplistNext(zl, fptr);

                // 标识这次操作为更新操作
                update = 1;

                // 删除旧的键值对
                zl = ziplistDelete(zl, &vptr);

                // 添加新的键值对
                zl = ziplistInsert(zl, vptr, value->ptr, sdslen(value->ptr));
            }
        }

        // 如果这不是更新操作，那么这就是一个添加操作
        if (!update)
        {
            // 将新的 field-value 对推入到 ziplist 的末尾
            zl = ziplistPush(zl, field->ptr, sdslen(field->ptr), ZIPLIST_TAIL);
            zl = ziplistPush(zl, value->ptr, sdslen(value->ptr), ZIPLIST_TAIL);
        }

        // 更新对象指针
        o->ptr = zl;

        // 释放临时对象
        decrRefCount(field);
        decrRefCount(value);

        // 检查在添加操作完成之后，是否需要将 ZIPLIST 编码转换成 HT 编码
        if (hashTypeLength(o) > server.hash_max_ziplist_entries)
            hashTypeConvert(o, REDIS_ENCODING_HT);
    }
    else if (o->encoding == REDIS_ENCODING_HT)
    {

        // 添加或替换键值对到字典
        // 添加返回 1 ，替换返回 0
        if (dictReplace(o->ptr, field, value))
        {
            incrRefCount(field);
        }
        else
        {
            update = 1;
        }

        incrRefCount(value);
    }
    else
    {
        //  redisPanic("Unknown hash encoding");
    }

    // 更新/添加指示变量
    return update;
}

int hashTypeDelete(robj *o, robj *field)
{
    int deleted = 0;

    if (o->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *zl, *fptr;

        field = getDecodedObject(field);

        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL)
        {
            // 定位到域
            fptr = ziplistFind(fptr, field->ptr, sdslen(field->ptr), 1);
            if (fptr != NULL)
            {
                // 删除键和值
                zl = ziplistDelete(zl, &fptr);
                zl = ziplistDelete(zl, &fptr);
                o->ptr = zl;
                deleted = 1;
            }
        }
        decrRefCount(field);
    }
    else if (o->encoding == REDIS_ENCODING_HT)
    {
        if (dictDelete((dict *)o->ptr, field) == REDIS_OK)
        {
            deleted = 1;
            // 删除成功时，看字典是否需要收缩
            if (htNeedsResize(o->ptr))
                dictResize(o->ptr);
        }
    }
    else
    {
        // redisPanic("Unknown hash encoding");
    }

    return deleted;
}

unsigned long hashTypeLength(robj *o)
{
    unsigned long length = ULONG_MAX;

    if (o->encoding == REDIS_ENCODING_ZIPLIST)
    {
        length = ziplistLen(o->ptr) / 2;
    }
    else if (o->encoding == REDIS_ENCODING_HT)
    {
        length = dictSize((dict *)o->ptr);
    }
    else
    {
        //redisPanic("Unknown hash encoding");
    }

    return length;
}

hashTypeIterator *hashTypeInitIterator(robj *subject)
{

    hashTypeIterator *hi = xm_malloc(sizeof(hashTypeIterator));

    // 指向对象
    hi->subject = subject;
    // 记录编码
    hi->encoding = subject->encoding;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST)
    {
        hi->fptr = NULL;
        hi->vptr = NULL;
    }
    else if (hi->encoding == REDIS_ENCODING_HT)
    {
        hi->di = dictGetIterator(subject->ptr);
    }
    else
    {
        // redisPanic("Unknown hash encoding");
    }

    // 返回迭代器
    return hi;
}

void hashTypeReleaseIterator(hashTypeIterator *hi)
{
    // 释放字典迭代器
    if (hi->encoding == REDIS_ENCODING_HT)
    {
        dictReleaseIterator(hi->di);
    }
    // 释放 ziplist 迭代器
    xm_free(hi);
}

int hashTypeNext(hashTypeIterator *hi)
{

    if (hi->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *zl;
        unsigned char *fptr, *vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        // 第一次执行时，初始化指针
        if (fptr == NULL)
        {
            fptr = ziplistIndex(zl, 0);
        }
        // 否则获取下一个迭代节点
        else
        {
            // 是值的下一个元素
            fptr = ziplistNext(zl, vptr);
        }

        // 迭代完毕，或者 ziplist 为空
        if (fptr == NULL)
            return REDIS_ERR;

        // 记录值的指针
        vptr = ziplistNext(zl, fptr);

        // 更新迭代器指针
        hi->fptr = fptr;
        hi->vptr = vptr;
    }
    else if (hi->encoding == REDIS_ENCODING_HT)
    {
        if ((hi->de = dictNext(hi->di)) == NULL)
            return REDIS_ERR;
    }
    else
    {
        // redisPanic("Unknown hash encoding");
    }

    // 迭代成功
    return REDIS_OK;
}

void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;
    // 取出键
    if (what & REDIS_HASH_KEY)
    {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
    }
    // 取出值
    else
    {
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
    }
}

void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst)
{
    redisAssert(hi->encoding == REDIS_ENCODING_HT);

    // 取出键
    if (what & REDIS_HASH_KEY)
    {
        *dst = dictGetKey(hi->de);
    }
    // 取出值
    else
    {
        *dst = dictGetVal(hi->de);
    }
}

robj *hashTypeCurrentObject(hashTypeIterator *hi, int what)
{
    robj *dst;

    if (hi->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        // 取出键或值
        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);

        // 创建键或值的对象
        if (vstr)
        {
            dst = createStringObject((char *)vstr, vlen);
        }
        else
        {
            dst = createStringObjectFromLongLong(vll);
        }
    }
    else if (hi->encoding == REDIS_ENCODING_HT)
    {
        // 取出键或者值
        hashTypeCurrentFromHashTable(hi, what, &dst);
        // 对对象的引用计数进行自增
        incrRefCount(dst);
    }
    else
    {
        // redisPanic("Unknown hash encoding");
    }

    // 返回对象
    return dst;
}
