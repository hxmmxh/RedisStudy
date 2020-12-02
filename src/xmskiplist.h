#ifndef XM_SKIPLIST_H
#define XM_SKIPLIST_H

#include <stdlib.h>
#include "xmobject.h"
#include "xmredis.h"
#include "xmdict.h"

// 表示开区间/闭区间范围的结构
// 两个数字表示范围
typedef struct
{
    // 最小值和最大值
    double min, max;
    // 指示最小值和最大值是否*不*包含在范围之内
    // 值为 1 表示不包含，值为 0 表示包含
    int minex, maxex;
} zrangespec;

// lexicographic式的表示范围的结构
// 两个字符串表示范围
// min="(a", max="c)", 表示的就是(a,c)
// 或者闭区间 min="[a", max="c]"表示[a,c]
typedef struct
{
    robj *min, *max; 
    int minex, maxex; 
} zlexrangespec;


// 检测给定值 value 是否大于（或大于等于）范围 spec 中的 min 项，返回1表明大于
// greater than
int zslValueGteMin(double value, zrangespec *spec);
// 检测给定值 value 是否小于（或小于等于）范围 spec 中的 max 项
// less than
int zslValueLteMax(double value, zrangespec *spec);
int zslLexValueGteMin(robj *value, zlexrangespec *spec);
int zslLexValueLteMax(robj *value, zlexrangespec *spec);
/************************************************************************************/

#define ZSKIPLIST_MAXLEVEL 32 // 跳跃表的最大层数
#define ZSKIPLIST_P 0.25      //用于随机获得新节点层数的函数，最大节点每高一层的概率为0.25

//跳跃表节点
typedef struct zskiplistNode
{
    // 成员对象
    robj *obj;
    // 分值
    double score;
    // 后退指针
    struct zskiplistNode *backward;
    // 层
    struct zskiplistLevel
    {
        // 前进指针，用于从表头向表尾方向访问节点
        struct zskiplistNode *forward;
        // 跨度
        unsigned int span;
    } level[];

} zskiplistNode;

//跳跃表
typedef struct zskiplist
{
    // 表头节点和表尾节点
    struct zskiplistNode *header, *tail;
    // 表中节点的数量（表头节点不计算在内）
    unsigned long length;
    // 表中层数最大的节点的层数（表头节点的层数不计算在内）
    int level;
} zskiplist;

//创建并返回一个新的跳跃表
zskiplist *zslCreate(void);
// 释放给定的跳跃表节点
void zslFreeNode(zskiplistNode *node);
//释放给定跳跃表，以及表中的所有节点
void zslFree(zskiplist *zsl);

// 创建一个成员为 obj ，分值为 score 的新节点， 并将这个新节点插入到跳跃表zsl中。 函数的返回值为新节点。
zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);
// 从跳跃表 zsl 中删除包含给定节点 score 并且带有指定对象 obj 的节点。删除成功返回1，失败返回0
int zslDelete(zskiplist *zsl, double score, robj *obj);

// 如果给定的分值范围包含在跳跃表的分值范围之内，那么返回 1 ，否则返回 0
int zslIsInRange(zskiplist *zsl, zrangespec *range);
// 如果给定的对象范围包含在跳跃表的对象范围之内，那么返回 1 ，否则返回 0
int zslIsInLexRange(zskiplist *zsl, zlexrangespec *range);
// 返回 zsl 中第一个分值符合 range 中指定范围的节点,如果没有，返回NULL
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
// 返回 zsl 中第一个分值符合 range 中指定范围的节点,如果没有，返回NULL
zskiplistNode *zslFirstInLexRange(zskiplist *zsl, zlexrangespec *range);
// 返回 zsl 中最后一个分值符合 range 中指定范围的节点。如果没有，返回NULL
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);
// //返回 zsl 中最后一个分值符合 range 中指定范围的节点。如果没有，返回NULL
zskiplistNode *zslLastInLexRange(zskiplist *zsl, zlexrangespec *range);


// 返回包含给定成员和分值的节点在跳跃表中的排位,表头排位为0，返回的排位以 1 为起始值。
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);
// 返回跳跃表在给定排位上的节点，排位的起始值为 1
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank);

// 删除所有分值在给定范围之内的节点。同时会从相应的字典中删除。返回值为被删除节点的数量
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict);
// 删除所有分值在给定范围之内的节点。同时会从相应的字典中删除。返回值为被删除节点的数量
unsigned long zslDeleteRangeByLex(zskiplist *zsl, zlexrangespec *range, dict *dict);
// 删除所有分值在给定排位之内的节点。同时会从相应的字典中删除。返回值为被删除节点的数量
// start 和 end 两个位置都是包含在内的。注意它们都是以 1 为起始值。
unsigned long zslDeleteRangeByRank(zskiplist *zsl, unsigned int start, unsigned int end, dict *dict);

#endif