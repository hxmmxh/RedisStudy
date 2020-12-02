#ifndef HXM_T_SET_H
#define HXM_T_SET_H

#include "xmobject.h"
#include "xmintset.h"
#include "xmdict.h"

#include "xmt_string.h"

#include "xmredis.h"
#include "xmserver.h"

// 集合对象的迭代器
typedef struct
{
    // 被迭代的对象
    robj *subject;
    // 对象的编码
    int encoding;
    // 索引值，编码为 intset 时使用
    int ii; 
    // 字典迭代器，编码为 HT 时使用
    dictIterator *di;
} setTypeIterator;

// 创建一个字典编码的集合对象。
robj *createSetObject(void);
// 创建一个 INTSET 编码的集合对象
robj *createIntsetObject(void);
// 释放集合对象
void freeSetObject(robj *o);

// 返回一个可以保存值 value 的集合。当对象的值可以被编码为整数时，返回 intset，否则，返回普通的哈希表
// 返回的集合对象是空的，还没有包含value
robj *setTypeCreate(robj *value);
// 在集合中添加一个对象。添加成功返回 1 ，如果元素已经存在，返回 0
int setTypeAdd(robj *subject, robj *value);
// 在集合中删除一个对象。删除成功返回 1 ，因为元素不存在而导致删除失败返回 0
int setTypeRemove(robj *subject, robj *value);
// 判断对象是否在集合中，查找成功返回1，失败返回0
int setTypeIsMember(robj *subject, robj *value);

// 创建并返回一个集合迭代器
setTypeIterator *setTypeInitIterator(robj *subject);
//  释放迭代器
void setTypeReleaseIterator(setTypeIterator *si);
// 取出被迭代器指向的当前集合元素,并往后移动迭代器
// 当编码为 intset 时，元素被指向到 llobj 参数
// 当编码为哈希表时，元素被指向到 eobj 参数
// 并且函数会返回被迭代集合的编码，方便识别。
// 当集合中的元素全部被迭代完毕时，函数返回 -1 。
// 被返回的对象是没有被增加引用计数的，
int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele);
// 总是返回一个新的、或者已经增加过引用计数的对象。
// 调用者在使用完对象之后，应该对对象调用 decrRefCount() 。
robj *setTypeNextObject(setTypeIterator *si);
// 从非空集合中随机取出一个元素。
int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele);
unsigned long setTypeSize(robj *subject);
// 将集合对象 setobj 的编码转换为 REDIS_ENCODING_HT 
// 新创建的结果字典会被预先分配为和原来的集合一样大。
void setTypeConvert(robj *subject, int enc);

#endif