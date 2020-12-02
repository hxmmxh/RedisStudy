#include "xmt_set.h"
#include "xmmalloc.h"

dictType setDictType = {
    dictEncObjHash,            /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictEncObjKeyCompare,      /* key compare */
    dictRedisObjectDestructor, /* key destructor */
    NULL                       /* val destructor */
};

robj *createSetObject(void)
{
    dict *d = dictCreate(&setDictType, NULL);
    robj *o = createObject(REDIS_SET, d);
    o->encoding = REDIS_ENCODING_HT;
    return o;
}

robj *createIntsetObject(void)
{
    intset *is = intsetNew();
    robj *o = createObject(REDIS_SET, is);
    o->encoding = REDIS_ENCODING_INTSET;
    return o;
}

robj *setTypeCreate(robj *value)
{
    if (isObjectRepresentableAsLongLong(value, NULL) == REDIS_OK)
        return createIntsetObject();
    return createSetObject();
}

int setTypeAdd(robj *subject, robj *value)
{
    long long llval;
    // 字典
    if (subject->encoding == REDIS_ENCODING_HT)
    {
        // 将 value 作为键， NULL 作为值，将元素添加到字典中
        if (dictAdd(subject->ptr, value, NULL) == DICT_OK)
        {
            incrRefCount(value);
            return 1;
        }
    }
    // 整数集合
    else if (subject->encoding == REDIS_ENCODING_INTSET)
    {
        // 如果对象的值可以编码为整数的话，那么将对象的值添加到 intset 中
        if (isObjectRepresentableAsLongLong(value, &llval) == REDIS_OK)
        {
            uint8_t success = 0;
            subject->ptr = intsetAdd(subject->ptr, llval, &success);
            if (success)
            {
                // 添加成功
                // 检查集合在添加新元素之后是否需要转换为字典
                if (intsetLen(subject->ptr) > server.set_max_intset_entries)
                    setTypeConvert(subject, REDIS_ENCODING_HT);
                return 1;
            }
        }
        // 如果对象的值不能编码为整数，那么将集合从 intset 编码转换为 HT 编码
        // 然后再执行添加操作
        else
        {
            setTypeConvert(subject, REDIS_ENCODING_HT);
            // 这里是一定可以添加成功的，因为加这个元素之前集合里都是整数，而这个元素不是
            dictAdd(subject->ptr, value, NULL);
            incrRefCount(value);
            return 1;
        }
    }
    else
    {
        // redisPanic("Unknown set encoding");
    }

    // 添加失败，元素已经存在
    return 0;
}

int setTypeRemove(robj *setobj, robj *value)
{
    long long llval;

    if (setobj->encoding == REDIS_ENCODING_HT)
    {
        // 从字典中删除键（集合元素）
        if (dictDelete(setobj->ptr, value) == DICT_OK)
        {
            // 看是否有必要在删除之后缩小字典的大小
            if (htNeedsResize(setobj->ptr))
                dictResize(setobj->ptr);
            return 1;
        }
    }
    else if (setobj->encoding == REDIS_ENCODING_INTSET)
    {
        // 如果对象的值可以编码为整数的话，那么尝试从 intset 中移除元素
        if (isObjectRepresentableAsLongLong(value, &llval) == REDIS_OK)
        {
            int success;
            setobj->ptr = intsetRemove(setobj->ptr, llval, &success);
            if (success)
                return 1;
        }
    }
    else
    {
        // redisPanic("Unknown set encoding");
    }

    // 删除失败
    return 0;
}

int setTypeIsMember(robj *subject, robj *value)
{
    long long llval;

    if (subject->encoding == REDIS_ENCODING_HT)
    {
        return dictFind((dict *)subject->ptr, value) != NULL;
    }
    else if (subject->encoding == REDIS_ENCODING_INTSET)
    {
        if (isObjectRepresentableAsLongLong(value, &llval) == REDIS_OK)
        {
            return intsetFind((intset *)subject->ptr, llval);
        }
    }
    else
    {
        // redisPanic("Unknown set encoding");
    }
    // 查找失败
    return 0;
}

setTypeIterator *setTypeInitIterator(robj *subject)
{
    setTypeIterator *si = xm_malloc(sizeof(setTypeIterator));
    // 指向被迭代的对象
    si->subject = subject;
    // 记录对象的编码
    si->encoding = subject->encoding;
    if (si->encoding == REDIS_ENCODING_HT)
    {
        si->di = dictGetIterator(subject->ptr);
    }
    else if (si->encoding == REDIS_ENCODING_INTSET)
    {
        si->ii = 0;
    }
    else
    {
        //redisPanic("Unknown set encoding");
    }
    return si;
}

void setTypeReleaseIterator(setTypeIterator *si)
{

    if (si->encoding == REDIS_ENCODING_HT)
        dictReleaseIterator(si->di);

    xm_free(si);
}

int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele)
{
    // 从字典中取出对象
    if (si->encoding == REDIS_ENCODING_HT)
    {
        // 更新迭代器
        dictEntry *de = dictNext(si->di);
        // 字典已迭代完
        if (de == NULL)
            return -1;
        // 返回节点的键（集合的元素）
        *objele = dictGetKey(de);
    }
    else if (si->encoding == REDIS_ENCODING_INTSET)
    {
        if (!intsetGet(si->subject->ptr, si->ii++, llele))
            return -1;
    }
    // 返回编码
    return si->encoding;
}

robj *setTypeNextObject(setTypeIterator *si)
{
    int64_t intele;
    robj *objele;
    int encoding;

    // 取出元素
    encoding = setTypeNext(si, &objele, &intele);
    // 总是为元素创建对象
    switch (encoding)
    {
    // 已为空
    case -1:
        return NULL;
    // INTSET 返回一个整数值，需要为这个值创建对象
    case REDIS_ENCODING_INTSET:
        return createStringObjectFromLongLong(intele);
    // HT 本身已经返回对象了，只需执行 incrRefCount()
    case REDIS_ENCODING_HT:
        incrRefCount(objele);
        return objele;
    default:
        // redisPanic("Unsupported encoding");
    }

    return NULL; /* just to suppress warnings */
}

int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele)
{

    if (setobj->encoding == REDIS_ENCODING_HT)
    {
        dictEntry *de = dictGetRandomKey(setobj->ptr);
        *objele = dictGetKey(de);
    }
    else if (setobj->encoding == REDIS_ENCODING_INTSET)
    {
        *llele = intsetRandom(setobj->ptr);
    }
    else
    {
        // redisPanic("Unknown set encoding");
    }

    return setobj->encoding;
}

unsigned long setTypeSize(robj *subject)
{

    if (subject->encoding == REDIS_ENCODING_HT)
    {
        return dictSize((dict *)subject->ptr);
    }
    else if (subject->encoding == REDIS_ENCODING_INTSET)
    {
        return intsetLen((intset *)subject->ptr);
    }
    else
    {
        // redisPanic("Unknown set encoding");
    }
}

void setTypeConvert(robj *setobj, int enc)
{

    setTypeIterator *si;

    if (enc == REDIS_ENCODING_HT)
    {
        int64_t intele;
        // 创建新字典
        dict *d = dictCreate(&setDictType, NULL);
        robj *element;

        // 预先扩展空间
        dictExpand(d, intsetLen(setobj->ptr));

        // 遍历集合，并将元素添加到字典中
        si = setTypeInitIterator(setobj);
        while (setTypeNext(si, NULL, &intele) != -1)
        {
            element = createStringObjectFromLongLong(intele);
            dictAdd(d, element, NULL);
        }
        setTypeReleaseIterator(si);

        // 更新集合的编码
        setobj->encoding = REDIS_ENCODING_HT;
        xm_free(setobj->ptr);
        // 更新集合的值对象
        setobj->ptr = d;
    }
    else
    {
        //redisPanic("Unsupported set conversion");
    }
}
