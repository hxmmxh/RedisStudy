#include "xmskiplist.h"
#include "xmsds.h"
#include "xmmalloc.h"

#include "xmserver.h"

#include "xmt_string.h"

#include <assert.h>


// 对比a，b对象表示的取值的大小
// 返回0表示相等，1表示a>b，-1表示a<b
int compareStringObjectsForLexRange(robj *a, robj *b)
{
    if (a == b)
        return 0;
    if (a == shared.minstring || b == shared.maxstring)
        return -1;
    if (a == shared.maxstring || b == shared.minstring)
        return 1;
    return compareStringObjects(a, b);
}

// 解析lex范围对象
// (foo means foo (open interval)
// [foo means foo (closed interval)
// - means the min string possible，
// + means the max string possible
// *dest保存解析后的对象， ex保存是闭区间（0）还是开区间（1）
int zslParseLexRangeItem(robj *item, robj **dest, int *ex)
{
    char *c = item->ptr;

    switch (c[0])
    {
    case '+':
        if (c[1] != '\0')
            return REDIS_ERR;
        *ex = 0;
        *dest = shared.maxstring;
        incrRefCount(shared.maxstring);
        return REDIS_OK;
    case '-':
        if (c[1] != '\0')
            return REDIS_ERR;
        *ex = 0;
        *dest = shared.minstring;
        incrRefCount(shared.minstring);
        return REDIS_OK;
    case '(':
        *ex = 1;
        *dest = createStringObject(c + 1, sdslen(c) - 1);
        return REDIS_OK;
    case '[':
        *ex = 0;
        *dest = createStringObject(c + 1, sdslen(c) - 1);
        return REDIS_OK;
    default:
        return REDIS_ERR;
    }
}

// 解析lex范围结构，成功返回REDIS_OK,且之后必须释放lex范围结构
static int zslParseLexRange(robj *min, robj *max, zlexrangespec *spec)
{
    // 如果是整数编码，肯定解析失败
    if (min->encoding == REDIS_ENCODING_INT ||
        max->encoding == REDIS_ENCODING_INT)
        return REDIS_ERR;

    spec->min = spec->max = NULL;

    if (zslParseLexRangeItem(min, &spec->min, &spec->minex) == REDIS_ERR ||
        zslParseLexRangeItem(max, &spec->max, &spec->maxex) == REDIS_ERR)
    {
        // 如果解析失败，记得要decr
        if (spec->min)
            decrRefCount(spec->min);
        if (spec->max)
            decrRefCount(spec->max);
        return REDIS_ERR;
    }
    else
    {
        return REDIS_OK;
    }
}

// 释放Lex范围结构
// 在调用zelParseLexRange成功后必须调用这个函数
void zslFreeLexRange(zlexrangespec *spec)
{
    decrRefCount(spec->min);
    decrRefCount(spec->max);
}

// 对 min 和 max 进行分析，并将区间的值保存在 spec 中。
// 分析成功返回 REDIS_OK ，分析出错导致失败返回 REDIS_ERR
static int zslParseRange(robj *min, robj *max, zrangespec *spec)
{
    char *eptr;

    // 默认为闭区间
    spec->minex = spec->maxex = 0;

    /* Parse the min-max interval. If one of the values is prefixed
     * by the "(" character, it's considered "open". For instance
     * ZRANGEBYSCORE zset (1.5 (2.5 will match min < x < max
     * ZRANGEBYSCORE zset 1.5 2.5 will instead match min <= x <= max */
    // 没有[]表示闭区间
    if (min->encoding == REDIS_ENCODING_INT)
    {
        // min 的值为整数
        spec->min = (long)min->ptr;
    }
    else
    {
        // min 对象为字符串，分析 min 的值并决定区间
        if (((char *)min->ptr)[0] == '(')
        {
            // double strtod(const char *str, char **endptr)
            // 把参数 str 所指向的字符串转换为一个浮点数（类型为 double 型）。
            // 如果 endptr 不为空，则指向转换中最后一个字符后的字符的指针会存储在 endptr 引用的位置。
            spec->min = strtod((char *)min->ptr + 1, &eptr);
            if (eptr[0] != '\0' || isnan(spec->min))
                return REDIS_ERR;
            spec->minex = 1;
        }
        else
        {
            spec->min = strtod((char *)min->ptr, &eptr);
            if (eptr[0] != '\0' || isnan(spec->min))
                return REDIS_ERR;
        }
    }

    if (max->encoding == REDIS_ENCODING_INT)
    {
        // max 的值为整数
        spec->max = (long)max->ptr;
    }
    else
    {
        // max 对象为字符串，分析 max 的值并决定区间
        if (((char *)max->ptr)[0] == '(')
        {
            spec->max = strtod((char *)max->ptr + 1, &eptr);
            if (eptr[0] != '\0' || isnan(spec->max))
                return REDIS_ERR;
            spec->maxex = 1;
        }
        else
        {
            spec->max = strtod((char *)max->ptr, &eptr);
            if (eptr[0] != '\0' || isnan(spec->max))
                return REDIS_ERR;
        }
    }

    return REDIS_OK;
}

/*************************************************************/

// 创建一个层数为level的跳跃表节点，并将节点的成员对象设置为obj，分值设置为 score，随后返回
static zskiplistNode *zslCreateNode(int level, double score, robj *obj);
//释放给定的跳跃表节点
static void zslFreeNode(zskiplistNode *node);
// 返回一个随机值，用作新跳跃表节点的层数。
static int zslRandomLevel(void);
static void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update);
/**************************************************/

static zskiplistNode *zslCreateNode(int level, double score, robj *obj)
{
    //还要分配Level[]的空间
    //因为Level是zskiplistNode的最后一个成员，所以其空间在free时只用free node就能释放
    zskiplistNode *zn = xm_malloc(sizeof(*zn) + level * sizeof(struct zskiplistLevel));
    zn->score = score;
    zn->obj = obj;
    return zn;
}

zskiplist *zslCreate(void)
{
    zskiplist *zsl;
    //分配空间和初始化
    zsl = xm_malloc(sizeof(*zsl));
    zsl->level = 1;
    zsl->length = 0;
    //初始化表头节点,层数固定为最大值
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
    //初始化表头节点的层
    int j;
    for (j = 0; j < ZSKIPLIST_MAXLEVEL; j++)
    {
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }
    zsl->header->backward = NULL;
    zsl->tail = NULL;
    return zsl;
}

void zslFreeNode(zskiplistNode *node)
{
    decrRefCount(node->obj);
    //level这个数组的空间不是额外分配的，不需要另外释放
    xm_free(node);
}

//释放给定跳跃表，以及表中的所有节点
void zslFree(zskiplist *zsl)
{
    zskiplistNode *node, *next;
    //从表头指向的第一个元素开始
    node = zsl->header->level[0].forward;
    //level[0]的跨度一定是1，保证能遍历到所有的节点
    while (node)
    {
        next = node->level[0].forward;
        zslFreeNode(node);
        node = next;
    }
    xm_free(zsl);
}

// 返回值介乎 1 和 ZSKIPLIST_MAXLEVEL 之间（包含 ZSKIPLIST_MAXLEVEL）
// 根据随机算法所使用的幂次定律，越大的值生成的几率越小
static int zslRandomLevel(void)
{
    int level = 1;
    //每次能成功的概率是ZSKIPLIST_P，也就是0.25
    while ((random() & 0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;
    //最终的level n的概率为ZSKIPLIST_P^(n-1)，也就是0.25^(n-1)
    return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj)
{
    zskiplistNode *x;
    int i, level;
    //记录每一层中最接近插入点的rank值，越下层的值肯定更大，更接近于最后的值
    //最终 rank[0] 的值就是新节点的前置节点的排位
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    //记录每一层中最接近插入点的节点
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL];

    x = zsl->header;
    //从当前表中的最高层开始，在各个层查找节点的插入位置
    for (i = zsl->level - 1; i >= 0; i--)
    {
        //当i=zsl->level-1 时说明才开始，从头开始查找
        //否则从上一层的位置处开始查找
        rank[i] = (i == (zsl->level - 1)) ? 0 : rank[i + 1];
        //沿着前进指针遍历跳跃表，找到最后一个小于插入值的节点
        while (x->level[i].forward &&
               //分值更小
               (x->level[i].forward->score < score ||
                //或者分值相同，但成员对象较小
                (x->level[i].forward->score == score && compareStringObjects(x->level[i].forward->obj, obj) < 0)))
        {
            //更新rank[i]
            rank[i] += x->level[i].span;
            //更新x
            x = x->level[i].forward;
        }
        // 记录将要和新节点相连接的节点
        update[i] = x;
    }
    // 接下来把新加入的节点插入到update[0]之后
    // 获取一个随机值作为新节点的层数
    level = zslRandomLevel();
    // 如果新节点的层数比表中其他节点的层数都要大
    // 那么初始化表头节点中未使用的层，并将它们记录到 update 数组中
    // 将来也指向新节点
    if (level > zsl->level)
    {
        // 初始化未使用层
        for (i = zsl->level; i < level; i++)
        {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].forward = NULL;
            //这里把跨度设为zsl->length会导致后面foward为NULL的层span为到末尾节点的距离
            update[i]->level[i].span = zsl->length;
        }
        // 更新表中节点最大层数
        zsl->level = level;
    }

    // 创建新节点
    x = zslCreateNode(level, score, obj);

    // 更新level比新节点低的前节点的属性
    // 将前面记录的指针指向新节点，并做相应的设置
    for (i = 0; i < level; i++)
    {
        // 设置新节点的 forward 指针
        x->level[i].forward = update[i]->level[i].forward;
        // 将沿途记录的各个节点的 forward 指针指向新节点
        update[i]->level[i].forward = x;
        // 计算新节点跨越的节点数量
        // update[i]->level[i].span是新节点插入之前前一个节点和后一个节点直接的距离
        // rank[0]-rank[i]+1 是第i层前一个节点和要插入的新节点直接的距离
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
        // 更新新节点插入之后，前面那个节点的 span 值
        // 其中的 +1 计算的是新节点
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    //level比新节点高的层简单的跨度加一，forward不变
    for (i = level; i < zsl->level; i++)
    {
        update[i]->level[i].span++;
    }
    // 设置新节点的后退指针
    // 第一个节点的backward指针是NULL，而不是指向表头
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    //修改新节点后一个节点的后退指针
    if (x->level[0].forward)
        x->level[0].forward->backward = x;
    //如果没有后一个节点，则新节点是表尾节点
    else
        zsl->tail = x;
    // 跳跃表的节点计数增一
    zsl->length++;
    return x;
}

//内部删除函数,update中记录每一层中最接近被删除节点的前一个节点
//不释放空间，释放空间的操作交给zslFreeNode
static void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update)
{
    int i;
    // 更新所有和被删除节点 x 有关的节点的指针，解除它们之间的关系
    for (i = 0; i < zsl->level; i++)
    {
        //如果forward指向了被删除的节点，同时更新forward和span
        if (update[i]->level[i].forward == x)
        {
            update[i]->level[i].span += x->level[i].span - 1;
            update[i]->level[i].forward = x->level[i].forward;
        }
        //否则只需要更新span
        else
        {
            update[i]->level[i].span -= 1;
        }
    }
    //更新后退指针
    if (x->level[0].forward)
    {
        x->level[0].forward->backward = x->backward;
    }
    else
    {
        zsl->tail = x->backward;
    }
    // 更新跳跃表最大层数（只在被删除节点是跳跃表中最高的节点时才执行）
    while (zsl->level > 1 && zsl->header->level[zsl->level - 1].forward == NULL)
        zsl->level--;
    // 跳跃表节点计数器减一
    zsl->length--;
}

int zslDelete(zskiplist *zsl, double score, robj *obj)
{
    //update中记录每一层中最接近被删除节点的前一个节点
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    // 遍历跳跃表，查找目标节点，并记录所有沿途节点
    x = zsl->header;
    //从最高层开始找最接近被删除节点的前一个节点
    for (i = zsl->level - 1; i >= 0; i--)
    {
        while (x->level[i].forward &&
               //分值更小
               (x->level[i].forward->score < score ||
                //或者分值相同，但成员对象较小，不同于zslGetRank用<=。是因为这里就是要记录前一个节点
                (x->level[i].forward->score == score && compareStringObjects(x->level[i].forward->obj, obj) < 0)))
            // 沿着前进指针移动
            x = x->level[i].forward;
        // 记录沿途节点
        update[i] = x;
    }
    //这个x是我们要找的节点
    x = x->level[0].forward;
    //只有在分值和对象都相同时才会删除
    if (x && score == x->score && equalStringObjects(x->obj, obj))
    {
        zslDeleteNode(zsl, x, update);
        zslFreeNode(x);
        return 1;
    }
    else
    {
        return 0;
    }

    return 0;
}


// minex为1表示不包含最小值，必须大于
int zslValueGteMin(double value, zrangespec *spec)
{
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

// 检测给定值 value 是否小于（或小于等于）范围 spec 中的 max 项
// maxex为1表示不包含最大值，必须小于
int zslValueLteMax(double value, zrangespec *spec)
{
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

// 判断value保存的对象表示的范围是否大于spec的范围的最小值
int zslLexValueGteMin(robj *value, zlexrangespec *spec)
{
    return spec->minex ? (compareStringObjectsForLexRange(value, spec->min) > 0) : (compareStringObjectsForLexRange(value, spec->min) >= 0);
}

// 判断value保存的对象表示的范围是否小于spec的范围的最大值
int zslLexValueLteMax(robj *value, zlexrangespec *spec)
{
    return spec->maxex ? (compareStringObjectsForLexRange(value, spec->max) < 0) : (compareStringObjectsForLexRange(value, spec->max) <= 0);
}

// 如果给定的分值范围包含在跳跃表的分值范围之内，那么返回 1 ，否则返回 0 。
// 只用检查头尾节点就行，因为跳跃表是有序的
int zslIsInRange(zskiplist *zsl, zrangespec *range)
{
    // 先排除总为空的范围值
    if (range->min > range->max ||
        (range->min == range->max && (range->minex || range->maxex)))
        return 0;
    zskiplistNode *x;
    // 检查最大分值
    x = zsl->tail;
    // 尾部节点为空，或者尾部节点都小于最小值
    if (x == NULL || !zslValueGteMin(x->score, range))
        return 0;
    // 第一个节点为空，或者大于最大值
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslValueLteMax(x->score, range))
        return 0;
    return 1;
}

int zslIsInLexRange(zskiplist *zsl, zlexrangespec *range)
{
    zskiplistNode *x;
    // 先排除总为空的范围值
    if (compareStringObjectsForLexRange(range->min, range->max) > 1 ||
        (compareStringObjects(range->min, range->max) == 0 &&
         (range->minex || range->maxex)))
        return 0;

    x = zsl->tail;

    if (x == NULL || !zslLexValueGteMin(x->obj, range))
        return 0;
    x = zsl->header->level[0].forward;
    if (x == NULL || !zslLexValueLteMax(x->obj, range))
        return 0;
    return 1;
}

zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range)
{
    // 如果给定的范围就不在跳跃表的范围内，直接失败返回
    if (!zslIsInRange(zsl, range))
        return NULL;
    zskiplistNode *x;
    int i;
    x = zsl->header;
    // 从高层往低层查找，每找一层都会更新x
    for (i = zsl->level - 1; i >= 0; i--)
    {
        // 当x的后一个对象大于范围中的最小值时，停止前进
        while (x->level[i].forward &&
               !zslValueGteMin(x->level[i].forward->score, range))
            x = x->level[i].forward;
    }
    // 再前进一次，就得到了所要的对象
    x = x->level[0].forward;
    // 还需要确认x小于范围的最大值
    // 前面调用zslIsInRange(zsl, range)只说明了跳跃表包含这个范围，没有保证一定有节点在这个范围内
    if (!zslValueLteMax(x->score, range))
        return NULL;
    return x;
}

zskiplistNode *zslFirstInLexRange(zskiplist *zsl, zlexrangespec *range)
{
    zskiplistNode *x;
    int i;

    // 如果给定的范围就不在跳跃表的范围内，直接失败返回
    if (!zslIsInLexRange(zsl, range))
        return NULL;

    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {
        // 当x的后一个对象大于范围中的最小值时，停止前进
        while (x->level[i].forward &&
               !zslLexValueGteMin(x->level[i].forward->obj, range))
            x = x->level[i].forward;
    }

    x = x->level[0].forward;

    if (!zslLexValueLteMax(x->obj, range))
        return NULL;
    return x;
}

zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range)
{
    if (!zslIsInRange(zsl, range))
        return NULL;
    zskiplistNode *x;
    int i;
    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {
        // 当x的后一个对象大于范围中的最大值时，停止前进
        while (x->level[i].forward &&
               zslValueLteMax(x->level[i].forward->score, range))
            x = x->level[i].forward;
    }
    if (!zslValueGteMin(x->score, range))
        return NULL;
    return x;
}

zskiplistNode *zslLastInLexRange(zskiplist *zsl, zlexrangespec *range)
{
    zskiplistNode *x;
    int i;

    if (!zslIsInLexRange(zsl, range))
        return NULL;

    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {

        while (x->level[i].forward &&
               zslLexValueLteMax(x->level[i].forward->obj, range))
            x = x->level[i].forward;
    }

    redisAssert(x != NULL);

    if (!zslLexValueGteMin(x->obj, range))
        return NULL;
    return x;
}

/* 如果没有包含给定分值和成员对象的节点，返回 0 ，否则返回排位。
因为跳跃表的表头也被计算在内，所以返回的排位以 1 为起始值*/
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o)
{
    zskiplistNode *x;
    unsigned long rank = 0;
    int i;

    x = zsl->header;
    // 从高层往低层查找，每找一层都会更新x和rank
    for (i = zsl->level - 1; i >= 0; i--)
    {
        // 当x的后一个对象小于要查找的对象时，继续前进
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
                // 比对分值
                (x->level[i].forward->score == score &&
                 // 比对成员对象，注意这里是<=0，所以会到x是要查找的值才停止
                 // 和zslDelete不同，是因为这里是要找到指定的那个元素
                 compareStringObjects(x->level[i].forward->obj, o) <= 0)))
        {

            // 累积跨越的节点数量，计算rank
            rank += x->level[i].span;
            // 沿着前进指针遍历跳跃表
            x = x->level[i].forward;
        }
        // 此时x的后一个对像大于要查找的对象，只有x是可能的要找的对象
        // 必须确保不仅分值相等，而且成员对象也要相等
        if (x->obj && equalStringObjects(x->obj, o))
        {
            return rank;
        }
    }
    // 没找到
    return 0;
}

zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank)
{
    zskiplistNode *x;
    unsigned long traversed = 0; // 统计rank
    int i;

    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {
        // 遍历跳跃表并累积越过的节点数量
        while (x->level[i].forward && (traversed + x->level[i].span) <= rank)
        {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        // 如果越过的节点数量已经等于 rank
        // 那么说明已经到达要找的节点
        if (traversed == rank)
        {
            return x;
        }
    }
    // 没找到目标节点
    return NULL;
}

unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    // 记录所有和被删除节点（们）有关的节点
    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {
        while (x->level[i].forward && (range->minex ? x->level[i].forward->score <= range->min : x->level[i].forward->score < range->min))
            x = x->level[i].forward;
        update[i] = x;
    }

    // 定位到给定范围开始的第一个节点
    x = x->level[0].forward;

    // 删除范围中的所有节点
    while (x &&
           (range->maxex ? x->score < range->max : x->score <= range->max))
    {
        // 记录下个节点的指针
        zskiplistNode *next = x->level[0].forward;
        zslDeleteNode(zsl, x, update);
        dictDelete(dict, x->obj);
        zslFreeNode(x);
        removed++;
        x = next;
    }
    return removed;
}

unsigned long zslDeleteRangeByLex(zskiplist *zsl, zlexrangespec *range, dict *dict)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long removed = 0;
    int i;

    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {
        while (x->level[i].forward &&
               !zslLexValueGteMin(x->level[i].forward->obj, range))
            x = x->level[i].forward;
        update[i] = x;
    }

    x = x->level[0].forward;

    while (x && zslLexValueLteMax(x->obj, range))
    {
        zskiplistNode *next = x->level[0].forward;

        // 从跳跃表中删除当前节点
        zslDeleteNode(zsl, x, update);
        // 从字典中删除当前节点
        dictDelete(dict, x->obj);
        // 释放当前跳跃表节点的结构
        zslFreeNode(x);

        // 增加删除计数器
        removed++;

        // 继续处理下个节点
        x = next;
    }

    // 返回被删除节点的数量
    return removed;
}

unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end, dict *dict)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned long traversed = 0, removed = 0;
    int i;

    // 沿着前进指针移动到指定排位的起始位置，并记录所有沿途指针
    x = zsl->header;
    for (i = zsl->level - 1; i >= 0; i--)
    {
        while (x->level[i].forward && (traversed + x->level[i].span) < start)
        {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    // 移动到排位的起始的第一个节点
    traversed++;
    x = x->level[0].forward;
    // 删除所有在给定排位范围内的节点
    while (x && traversed <= end)
    {
        // 记录下一节点的指针
        zskiplistNode *next = x->level[0].forward;
        // 从跳跃表中删除节点
        zslDeleteNode(zsl, x, update);
        // 从字典中删除节点
        dictDelete(dict, x->obj);
        // 释放节点结构
        zslFreeNode(x);
        // 为删除计数器增一
        removed++;
        // 为排位计数器增一
        traversed++;
        // 处理下个节点
        x = next;
    }
    // 返回被删除节点的数量
    return removed;
}