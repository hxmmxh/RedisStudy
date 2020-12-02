#ifndef HXM_T_HASH_H
#define HXM_T_HASH_H

#include "xmobject.h"
#include "xmdict.h"
#include "xmzplist.h"
#include "xmt_string.h"

#include "xmserver.h"
#include "xmclient.h"

// 哈希对象的迭代器
typedef struct
{
    // 被迭代的哈希对象
    robj *subject;
    // 哈希对象的编码
    int encoding;

    // 域指针和值指针
    // 在迭代 ZIPLIST 编码的哈希对象时使用
    unsigned char *fptr, *vptr;

    // 字典迭代器和指向当前迭代字典节点的指针
    // 在迭代 HT 编码的哈希对象时使用
    dictIterator *di;
    dictEntry *de;
} hashTypeIterator;


#define REDIS_HASH_KEY 1
#define REDIS_HASH_VALUE 2



// 创建一个压缩列表编码的哈希对象
robj *createHashObject(void);

// 将 ZIPLIST 编码转换成 HT 编码
void hashTypeConvert(robj *o, int enc);
// 对 argv 数组中的多个对象进行检查，看是否需要将对象的编码从ZIPLIST转换成 HT
void hashTypeTryConversion(robj *subject, robj **argv, int start, int end);
// 当编码为HT时，对o1和o2进行编码以节省内存
void hashTypeTryObjectEncoding(robj *subject, robj **o1, robj **o2);
// 从 hash 中取出域 field 的值，并返回一个值对象。找到返回值对象，没找到返回 NULL 。
robj *hashTypeGetObject(robj *o, robj *key);
// 检查给定域 feild 是否存在于 hash 对象 o 中。存在返回 1 ，不存在返回 0 。
int hashTypeExists(robj *o, robj *key);
// 将给定的 field-value 对添加到 hash 中，如果 field 已经存在，那么删除旧的值，并关联新值
// 返回 0 表示元素已经存在，这次函数调用执行的是更新操作。返回 1 则表示函数执行的是新添加操作
int hashTypeSet(robj *o, robj *key, robj *value);
// 将给定 field 及其 value 从哈希表中删除,删除成功返回 1，因为键不存在而造成的删除失败返回 0
int hashTypeDelete(robj *o, robj *key);
// 返回哈希对象的 field-value 对数量
unsigned long hashTypeLength(robj *o);


// 创建一个哈希类型的迭代器
hashTypeIterator *hashTypeInitIterator(robj *subject);
// 释放迭代器
void hashTypeReleaseIterator(hashTypeIterator *hi);
// 获取哈希中的下一个节点，并将它保存到迭代器。 如果获取成功，返回 REDIS_OK ，如果已经没有元素可获取（为空，或者迭代完毕），那么返回 REDIS_ERR 
int hashTypeNext(hashTypeIterator *hi);
// 从压缩列表编码的哈希中，取出迭代器指针当前指向节点的域或值。
// what决定取出值还是键
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll);
// 从字典编码的哈希中取出所指向节点的 field 或者 value 。
void hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what, robj **dst);
//  这个函数返回一个增加了引用计数的对象，或者一个新对象。
//  当使用完返回对象之后，调用者需要对对象执行 decrRefCount() 。
robj *hashTypeCurrentObject(hashTypeIterator *hi, int what);
// robj *hashTypeLookupWriteOrCreate(redisClient *c, robj *key);

#endif
