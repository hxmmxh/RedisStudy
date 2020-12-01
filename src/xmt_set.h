#ifndef HXM_T_SET_H
#define HXM_T_SET_H

#include "xmobject.h"
#include "xmintset.h"
#include "xmdict.h"

// 创建一个字典编码的集合对象。
robj *createSetObject(void);
// 创建一个 INTSET 编码的集合对象
robj *createIntsetObject(void);
// 释放集合对象
void freeSetObject(robj *o);


#endif