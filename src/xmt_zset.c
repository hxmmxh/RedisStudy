#include "xmt_zset.h"
#include "xmmalloc.h"

dictType zsetDictType = {
    dictEncObjHash,            /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictEncObjKeyCompare,      /* key compare */
    dictRedisObjectDestructor, /* key destructor */
    NULL                       /* val destructor */
};

robj *createZsetObject(void)
{
    zset *zs = xm_malloc(sizeof(*zs));
    robj *o;
    zs->dict = dictCreate(&zsetDictType, NULL);
    zs->zsl = zslCreate();
    o = createObject(REDIS_ZSET, zs);
    o->encoding = REDIS_ENCODING_SKIPLIST;
    return o;
}

// 创建一个 ZIPLIST 编码的有序集合
robj *createZsetZiplistObject(void)
{
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_ZSET, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

void freeZsetObject(robj *o)
{
    zset *zs;
    switch (o->encoding)
    {
    case REDIS_ENCODING_SKIPLIST:
        zs = o->ptr;
        dictRelease(zs->dict);
        zslFree(zs->zsl);
        xm_free(zs);
        break;
    case REDIS_ENCODING_ZIPLIST:
        xm_free(o->ptr);
        break;
    default:
        //redisPanic("Unknown sorted set encoding");
    }
}

double zzlGetScore(unsigned char *sptr)
{
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    char buf[128];
    double score;

    // 取出节点值
    ziplistGet(sptr, &vstr, &vlen, &vlong);

    // 如果是字符串
    if (vstr)
    {
        memcpy(buf, vstr, vlen);
        buf[vlen] = '\0';
        score = strtod(buf, NULL);
    }
    // 如果是整数
    else
    {
        score = vlong;
    }
    return score;
}

// 返回保存着键的对象，使用完记得减引用
robj *ziplistGetObject(unsigned char *sptr)
{
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;

    ziplistGet(sptr, &vstr, &vlen, &vlong);

    if (vstr)
    {
        return createStringObject((char *)vstr, vlen);
    }
    else
    {
        return createStringObjectFromLongLong(vlong);
    }
}

/* 将 对象eptr 中的元素和 字符串cstr 进行对比。
 *
 * 相等返回 0 ，
 * 不相等并且 eptr 的字符串比 cstr 大时，返回正整数。
 * 不相等并且 eptr 的字符串比 cstr 小时，返回负整数*/
int zzlCompareElements(unsigned char *eptr, unsigned char *cstr, unsigned int clen)
{
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    unsigned char vbuf[32];
    int minlen, cmp;

    // 取出节点中的字符串值，以及它的长度
    ziplistGet(eptr, &vstr, &vlen, &vlong);
    // 取出的是整数
    if (vstr == NULL)
    {
        // 把它转换成字符串
        vlen = ll2string((char *)vbuf, sizeof(vbuf), vlong);
        vstr = vbuf;
    }

    // 对比
    minlen = (vlen < clen) ? vlen : clen;
    cmp = memcmp(vstr, cstr, minlen);
    if (cmp == 0)
        return vlen - clen;
    return cmp;
}

// 返回跳跃表包含的元素数量
unsigned int zzlLength(unsigned char *zl)
{
    return ziplistLen(zl) / 2;
}

void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr)
{
    unsigned char *_eptr, *_sptr;
    // 指向下个成员
    _eptr = ziplistNext(zl, *sptr);
    if (_eptr != NULL)
    {
        // 指向下个分值
        _sptr = ziplistNext(zl, _eptr);
    }
    else
    {
        _sptr = NULL;
    }
    *eptr = _eptr;
    *sptr = _sptr;
}

void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr)
{
    unsigned char *_eptr, *_sptr;

    _sptr = ziplistPrev(zl, *eptr);
    if (_sptr != NULL)
    {
        _eptr = ziplistPrev(zl, _sptr);
    }
    else
    {
        _eptr = NULL;
    }

    *eptr = _eptr;
    *sptr = _sptr;
}

/* 如果给定的 ziplist 有至少一个节点符合 range 中指定的范围，
 * 那么函数返回 1 ，否则返回 0 。
 */
int zzlIsInRange(unsigned char *zl, zrangespec *range)
{
    unsigned char *p;
    double score;

    // 如果给定的范围为空，那么肯定失败
    if (range->min > range->max ||
        (range->min == range->max && (range->minex || range->maxex)))
        return 0;

    // 这里的压缩列表是有序的
    // 取出 ziplist 中的最大分值，并和 range 的最大值对比
    p = ziplistIndex(zl, -1);
    if (p == NULL)
        return 0;
    score = zzlGetScore(p);
    if (!zslValueGteMin(score, range))
        return 0;

    // 取出 ziplist 中的最小值，并和 range 的最小值进行对比
    p = ziplistIndex(zl, 1);
    score = zzlGetScore(p);
    if (!zslValueLteMax(score, range))
        return 0;
    // ziplist 有至少一个节点符合范围
    return 1;
}

/* 返回第一个 score 值在给定范围内的节点
 * 如果没有节点的 score 值在给定范围，返回 NULL */
unsigned char *zzlFirstInRange(unsigned char *zl, zrangespec *range)
{
    // 从表头开始遍历
    unsigned char *eptr = ziplistIndex(zl, 0), *sptr;
    double score;

    // 如果没有在范围内的节点，直接返回NULL
    if (!zzlIsInRange(zl, range))
        return NULL;

    // 分值在 ziplist 中是从小到大排列的
    // 从表头向表尾遍历
    while (eptr != NULL)
    {
        sptr = ziplistNext(zl, eptr);

        score = zzlGetScore(sptr);
        if (zslValueGteMin(score, range))
        {
            // 遇上第一个符合范围的分值，
            // 返回它的节点指针
            if (zslValueLteMax(score, range))
                return eptr;
            return NULL;
        }

        eptr = ziplistNext(zl, sptr);
    }

    return NULL;
}

/* 返回 score 值在给定范围内的最后一个节点*
 * 没有元素包含它时，返回 NULL*/
unsigned char *zzlLastInRange(unsigned char *zl, zrangespec *range)
{
    // 从表尾开始遍历
    unsigned char *eptr = ziplistIndex(zl, -2), *sptr;
    double score;

    if (!zzlIsInRange(zl, range))
        return NULL;

    // 在有序的 ziplist 里从表尾到表头遍历
    while (eptr != NULL)
    {
        sptr = ziplistNext(zl, eptr);

        // 获取节点的 score 值
        score = zzlGetScore(sptr);
        if (zslValueLteMax(score, range))
        {
            if (zslValueGteMin(score, range))
                return eptr;
            return NULL;
        }

        sptr = ziplistPrev(zl, eptr);
        if (sptr != NULL)
            eptr = ziplistPrev(zl, sptr);
        else
            eptr = NULL;
    }

    return NULL;
}

static int zzlLexValueGteMin(unsigned char *p, zlexrangespec *spec)
{
    robj *value = ziplistGetObject(p);
    int res = zslLexValueGteMin(value, spec);
    decrRefCount(value);
    return res;
}

static int zzlLexValueLteMax(unsigned char *p, zlexrangespec *spec)
{
    robj *value = ziplistGetObject(p);
    int res = zslLexValueLteMax(value, spec);
    decrRefCount(value);
    return res;
}

// 这里是比较对象，不是分值
int zzlIsInLexRange(unsigned char *zl, zlexrangespec *range)
{
    unsigned char *p;

    if (compareStringObjectsForLexRange(range->min, range->max) > 1 ||
        (compareStringObjects(range->min, range->max) == 0 &&
         (range->minex || range->maxex)))
        return 0;

    p = ziplistIndex(zl, -2);
    if (p == NULL)
        return 0;
    if (!zzlLexValueGteMin(p, range))
        return 0;

    p = ziplistIndex(zl, 0);
    if (!zzlLexValueLteMax(p, range))
        return 0;

    return 1;
}

unsigned char *zzlFirstInLexRange(unsigned char *zl, zlexrangespec *range)
{
    unsigned char *eptr = ziplistIndex(zl, 0), *sptr;

    if (!zzlIsInLexRange(zl, range))
        return NULL;

    while (eptr != NULL)
    {
        // 如果大于最小值
        if (zzlLexValueGteMin(eptr, range))
        {
            // 并且小于最大值
            if (zzlLexValueLteMax(eptr, range))
                return eptr;
            return NULL;
        }

        // 移动到下一个元素
        sptr = ziplistNext(zl, eptr);
        eptr = ziplistNext(zl, sptr);
    }

    return NULL;
}

unsigned char *zzlLastInLexRange(unsigned char *zl, zlexrangespec *range)
{
    // 最后一个键值
    unsigned char *eptr = ziplistIndex(zl, -2), *sptr;

    if (!zzlIsInLexRange(zl, range))
        return NULL;

    while (eptr != NULL)
    {
        // 如果小于最大值
        if (zzlLexValueLteMax(eptr, range))
        {
            // 并且大于最小值
            if (zzlLexValueGteMin(eptr, range))
                return eptr;
            return NULL;
        }
        // 移动到上一个元素
        sptr = ziplistPrev(zl, eptr);
        if (sptr != NULL)
            eptr = ziplistPrev(zl, sptr);
        else
            eptr = NULL;
    }

    return NULL;
}

/* 从 ziplist 编码的有序集合中查找 ele 成员，并将它的分值保存到 score 。
 * 寻找成功返回指向成员 ele 的指针，查找失败返回 NULL 。
 */
unsigned char *zzlFind(unsigned char *zl, robj *ele, double *score)
{
    // 定位到首个元素
    unsigned char *eptr = ziplistIndex(zl, 0), *sptr;
    // 解码成员
    ele = getDecodedObject(ele);
    // 遍历整个 ziplist ，查找元素（确认成员存在，并且取出它的分值）
    while (eptr != NULL)
    {
        // 指向分值
        sptr = ziplistNext(zl, eptr);
        // 比对成员
        if (ziplistCompare(eptr, ele->ptr, sdslen(ele->ptr)))
        {
            // 成员匹配，取出分值
            if (score != NULL)
                *score = zzlGetScore(sptr);
            decrRefCount(ele);
            return eptr;
        }
        eptr = ziplistNext(zl, sptr);
    }
    decrRefCount(ele);
    // 没有找到
    return NULL;
}

// 从 ziplist 中删除 eptr 所指定的有序集合元素（包括成员和分值）
unsigned char *zzlDelete(unsigned char *zl, unsigned char *eptr)
{
    unsigned char *p = eptr;
    zl = ziplistDelete(zl, &p);
    zl = ziplistDelete(zl, &p);
    return zl;
}

/* 将带有给定成员和分值的新节点插入到 eptr 所指向的节点的前面，
 * 如果 eptr 为 NULL ，那么将新节点插入到 ziplist 的末端。
 * 函数返回插入操作完成之后的 ziplist*/
unsigned char *zzlInsertAt(unsigned char *zl, unsigned char *eptr, robj *ele, double score)
{
    unsigned char *sptr;
    char scorebuf[128];
    int scorelen;
    size_t offset;

    // 把double值转化成字符串
    scorelen = d2string(scorebuf, sizeof(scorebuf), score);

    // 插入到表尾，或者空表
    if (eptr == NULL)
    {
        // 先推入元素
        zl = ziplistPush(zl, ele->ptr, sdslen(ele->ptr), ZIPLIST_TAIL);
        // 后推入分值
        zl = ziplistPush(zl, (unsigned char *)scorebuf, scorelen, ZIPLIST_TAIL);
    }
    // 插入到某个节点的前面
    else
    {
        // 插入成员
        offset = eptr - zl;
        zl = ziplistInsert(zl, eptr, ele->ptr, sdslen(ele->ptr));
        eptr = zl + offset;
        // 将分值插入在成员之后
        sptr = ziplistNext(zl, eptr);
        zl = ziplistInsert(zl, sptr, (unsigned char *)scorebuf, scorelen);
    }

    return zl;
}

/* 将 ele 成员和它的分值 score 添加到 ziplist 里面
 * ziplist 里的各个节点按 score 值从小到大排列
 * 这个函数假设 elem 不存在于有序集*/
unsigned char *zzlInsert(unsigned char *zl, robj *ele, double score)
{

    // 指向 ziplist 第一个节点
    unsigned char *eptr = ziplistIndex(zl, 0), *sptr;
    double s;

    // 解码值
    ele = getDecodedObject(ele);

    // 遍历整个 ziplist
    while (eptr != NULL)
    {
        // 取出分值
        sptr = ziplistNext(zl, eptr);
        s = zzlGetScore(sptr);

        if (s > score)
        {
            // 遇到第一个 score 值比输入 score 大的节点
            // 那么将新节点插入在这个节点的前面，
            // 让节点在 ziplist 里根据 score 从小到大排列
            zl = zzlInsertAt(zl, eptr, ele, score);
            break;
        }
        else if (s == score)
        {
            // 如果输入 score 和节点的 score 相同
            // 那么根据 member 的字符串位置来决定新节点的插入位置
            // 如果还是eptr更大，则在这里就插入
            // 但如果eptr更小，继续循环，在下一个节点插入
            if (zzlCompareElements(eptr, ele->ptr, sdslen(ele->ptr)) > 0)
            {
                zl = zzlInsertAt(zl, eptr, ele, score);
                break;
            }
        }

        // 输入 score 比节点的 score 值要大
        // 移动到下一个节点
        eptr = ziplistNext(zl, sptr);
    }

    // 如果都没找到，到了队尾
    if (eptr == NULL)
        zl = zzlInsertAt(zl, NULL, ele, score);

    decrRefCount(ele);
    return zl;
}

unsigned char *zzlDeleteRangeByScore(unsigned char *zl, zrangespec *range, unsigned long *deleted)
{
    unsigned char *eptr, *sptr;
    double score;
    unsigned long num = 0;

    if (deleted != NULL)
        *deleted = 0;

    // 指向 ziplist 中第一个符合范围的节点
    eptr = zzlFirstInRange(zl, range);
    if (eptr == NULL)
        return zl;

    // 一直删除节点，直到遇到不在范围内的值为止
    // 节点中的值都是有序的
    // 如果eptr为空，那么ziplistNext返回的也是空
    while ((sptr = ziplistNext(zl, eptr)) != NULL)
    {
        score = zzlGetScore(sptr);
        // 如果在范围内
        if (zslValueLteMax(score, range))
        {
            // 同时删除键和值
            zl = ziplistDelete(zl, &eptr);
            zl = ziplistDelete(zl, &eptr);
            num++;
        }
        else
        {
            break;
        }
    }

    if (deleted != NULL)
        *deleted = num;
    return zl;
}

unsigned char *zzlDeleteRangeByLex(unsigned char *zl, zlexrangespec *range, unsigned long *deleted)
{
    unsigned char *eptr, *sptr;
    unsigned long num = 0;

    if (deleted != NULL)
        *deleted = 0;

    eptr = zzlFirstInLexRange(zl, range);
    if (eptr == NULL)
        return zl;

    while ((sptr = ziplistNext(zl, eptr)) != NULL)
    {
        if (zzlLexValueLteMax(eptr, range))
        {
            zl = ziplistDelete(zl, &eptr);
            zl = ziplistDelete(zl, &eptr);
            num++;
        }
        else
        {
            break;
        }
    }

    if (deleted != NULL)
        *deleted = num;

    return zl;
}

/* 删除 ziplist 中所有在给定排位范围内的元素。
 * start 和 end 索引都是包括在内的。并且它们都以 1 为起始值。
 * 如果 deleted 不为 NULL ，那么在删除操作完成之后，将删除元素的数量保存到 *deleted 中*/
unsigned char *zzlDeleteRangeByRank(unsigned char *zl, unsigned int start, unsigned int end, unsigned long *deleted)
{
    unsigned int num = (end - start) + 1;

    if (deleted)
        *deleted = num;

    // 每个元素占用两个节点，所以删除的个数其实要乘以 2
    // 并且因为 ziplist 的索引以 0 为起始值，而 zzl 的起始值为 1 ，
    // 所以需要 start - 1
    zl = ziplistDeleteRange(zl, 2 * (start - 1), 2 * num);

    return zl;
}

/**************************************************************************/
unsigned int zsetLength(robj *zobj)
{
    int length = -1;
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST)
    {
        length = zzlLength(zobj->ptr);
    }
    else if (zobj->encoding == REDIS_ENCODING_SKIPLIST)
    {
        length = ((zset *)zobj->ptr)->zsl->length;
    }
    else
    {
        //redisPanic("Unknown sorted set encoding");
    }

    return length;
}

// 将跳跃表对象 zobj 的底层编码转换为 encoding 。
void zsetConvert(robj *zobj, int encoding)
{
    zset *zs;
    zskiplistNode *node, *next;
    robj *ele;
    double score;

    if (zobj->encoding == encoding)
        return;

    // 从 ZIPLIST 编码转换为 SKIPLIST 编码
    if (zobj->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        assert(encoding == REDIS_ENCODING_SKIPLIST);

        // 创建有序集合结构
        zs = xm_malloc(sizeof(*zs));
        // 字典
        zs->dict = dictCreate(&zsetDictType, NULL);
        // 跳跃表
        zs->zsl = zslCreate();

        // 有序集合在 ziplist 中的排列：
        //
        // | member-1 | score-1 | member-2 | score-2 | ... |
        //
        // 指向 ziplist 中的首个节点（保存着元素成员）
        eptr = ziplistIndex(zl, 0);
        // 指向 ziplist 中的第二个节点（保存着元素分值）
        sptr = ziplistNext(zl, eptr);

        // 遍历所有 ziplist 节点，并将元素的成员和分值添加到有序集合中
        while (eptr != NULL)
        {
            // 取出分值
            score = zzlGetScore(sptr);
            // 取出成员
            ziplistGet(eptr, &vstr, &vlen, &vlong);
            if (vstr == NULL)
                ele = createStringObjectFromLongLong(vlong);
            else
                ele = createStringObject((char *)vstr, vlen);

            // 将成员和分值分别关联到跳跃表和字典中
            node = zslInsert(zs->zsl, score, ele);
            dictAdd(zs->dict, ele, &node->score);
            incrRefCount(ele);

            // 移动指针，指向下个元素
            zzlNext(zl, &eptr, &sptr);
        }
        // 释放原来的 ziplist
        xm_free(zobj->ptr);

        // 更新对象的值，以及编码方式
        zobj->ptr = zs;
        zobj->encoding = REDIS_ENCODING_SKIPLIST;
    }
    // 从 SKIPLIST 转换为 ZIPLIST 编码
    else if (zobj->encoding == REDIS_ENCODING_SKIPLIST)
    {
        // 新的 ziplist
        unsigned char *zl = ziplistNew();

        assert(encoding == REDIS_ENCODING_ZIPLIST);

        // 指向跳跃表
        zs = zobj->ptr;

        // 先释放字典，因为只需要跳跃表就可以遍历整个有序集合了
        dictRelease(zs->dict);

        // 指向跳跃表首个节点
        node = zs->zsl->header->level[0].forward;

        // 释放跳跃表表头和跳跃表结构
        xm_free(zs->zsl->header);
        xm_free(zs->zsl);

        // 遍历跳跃表，取出里面的元素，并将它们添加到 ziplist
        while (node)
        {
            // 取出解码后的值对象
            ele = getDecodedObject(node->obj);
            // 添加元素到 ziplist，都是有序的
            zl = zzlInsertAt(zl, NULL, ele, node->score);
            decrRefCount(ele);
            // 沿着跳跃表的第 0 层前进
            next = node->level[0].forward;
            zslFreeNode(node);
            node = next;
        }

        xm_free(zs);

        // 更新对象的值，以及对象的编码方式
        zobj->ptr = zl;
        zobj->encoding = REDIS_ENCODING_ZIPLIST;
    }
    else
    {
        // redisPanic("Unknown sorted set encoding");
    }
}
