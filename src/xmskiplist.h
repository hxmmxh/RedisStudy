#ifndef XM_SKIPLIST_H
#define XM_SKIPLIST_H

#include <stdlib.h>

#define REDIS_LRU_BITS 24
typedef struct redisObject
{
    // 类型
    unsigned type : 4;
    // 编码
    unsigned encoding : 4;
    // 对象最后一次被访问的时间
    unsigned lru : REDIS_LRU_BITS; /* lru time (relative to server.lruclock) */
    // 引用计数
    int refcount;
    // 指向实际值的指针
    void *ptr;
} robj;

// 表示开区间/闭区间范围的结构
typedef struct
{
    // 最小值和最大值
    double min, max;
    // 指示最小值和最大值是否*不*包含在范围之内
    // 值为 1 表示不包含，值为 0 表示包含
    int minex, maxex;
} zrangespec;

#define ZSKIPLIST_MAXLEVEL 32 /* Should be enough for 2^32 elements */
#define ZSKIPLIST_P 0.25      /* Skiplist P = 1/4 */

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
//释放给定跳跃表，以及表中的所有节点
void zslFree(zskiplist *zsl);

zskiplistNode *zslInsert(zskiplist *zsl, double score, robj *obj);
int zslDelete(zskiplist *zsl, double score, robj *obj);

int zslIsInRange(zskiplist *zsl, zrangespec *range);
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);

double zzlGetScore(unsigned char *sptr);

unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);
zskiplistNode *zslGetElementByRank(zskiplist *zsl, unsigned long rank);

#endif