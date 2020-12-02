#ifndef HXM_T_ZSET_H
#define HXM_T_ZSET_H

#include "xmobject.h"
#include "xmdict.h"
#include "xmskiplist.h"
#include "xmzplist.h"

#include "xmt_string.h"

#include "xmredis.h"
#include "xmserver.h"

typedef struct zset {

    // 字典，键为成员，值为分值
    // 用于支持 O(1) 复杂度的按成员取分值操作
    dict *dict;

    // 跳跃表，按分值排序成员
    // 用于支持平均复杂度为 O(log N) 的按分值定位成员操作
    // 以及范围操作
    zskiplist *zsl;

} zset;


robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
void freeZsetObject(robj *o);

//  取出 sptr 指向节点所保存的有序集合元素的分值
double zzlGetScore(unsigned char *sptr);
// 根据 eptr 和 sptr ，移动它们分别指向下个成员和下个分值。如果后面已经没有元素，那么两个指针都被设为 NULL 
void zzlNext(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);
// 根据 eptr 和 sptr ，移动它们分别指向前一个成员和分值。如果前面已经没有元素，那么两个指针都被设为 NULL 
void zzlPrev(unsigned char *zl, unsigned char **eptr, unsigned char **sptr);


unsigned int zsetLength(robj *zobj);
void zsetConvert(robj *zobj, int encoding);
unsigned long zslGetRank(zskiplist *zsl, double score, robj *o);



#endif