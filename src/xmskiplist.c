#include "xmskiplist.h"
#include "xmmalloc.h"

// 创建一个层数为 level 的跳跃表节点，并将节点的成员对象设置为 obj ，分值设置为 score 。
// 返回值为新创建的跳跃表节点
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

//释放给定的跳跃表节点
void zslFreeNode(zskiplistNode *node)
{
    //暂时先不实现释放对象的功能
    //decrRefCount(node->obj);
    //level这个数组的空间不是额外分配的，不需要另外释放
    xm_free(node);
}