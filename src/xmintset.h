#ifndef HXM_INTSET_H
#define HXM_INTSET_H

#include <stdint.h>

typedef struct intset {
    
    // 编码方式
    uint32_t encoding;

    // 集合包含的元素数量
    uint32_t length;

    // 保存元素的数组
    int8_t contents[];

} intset;

//创建一个新的空整数集合
intset *intsetNew(void);
//将给定元素添加到整数集合里面
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);
//从整数集合中移除给定元素
intset *intsetRemove(intset *is, int64_t value, int *success);
//检查给定值是否存在于集合
uint8_t intsetFind(intset *is, int64_t value);
//从整数集合中随机返回一个元素
int64_t intsetRandom(intset *is);
//取出底层数组在给定索引上的元素
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);
//返回整数集合包含的元素个数
uint32_t intsetLen(intset *is);
//返回整数集合占用的内存字节数
size_t intsetBlobLen(intset *is);



#endif