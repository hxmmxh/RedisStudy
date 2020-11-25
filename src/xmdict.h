#ifndef HXM_DICT_H
#define HXM_DICT_H

#include <stdint.h>

// 字典的操作状态
// 操作成功
#define DICT_OK 0
// 操作失败（或出错）
#define DICT_ERR 1

//哈希表的初始大小
#define DICT_HT_INITIAL_SIZE 4

//哈希表节点
typedef struct dictEntry
{
    //键
    void *key;
    //值，可以是一个指针， 或者是一个 uint64_t 整数， 又或者是一个 int64_t 整数。
    union
    {
        void *val;
        uint64_t u64;
        int64_t s64;
    } v;
    //指向另一个哈希表节点的指针， 这个指针可以将多个哈希值相同的键值对连接在一次， 以此来解决键冲突（collision）的问题。
    struct dictEntry *next;
} dictEntry;

//哈希表
typedef struct dictht
{
    // 哈希表节点数组
    dictEntry **table;
    // 哈希表大小,一定是2的整数次方
    unsigned long size;
    // 哈希表大小掩码，用于计算索引值，总是等于 size - 1，二进制一定是1111....
    unsigned long sizemask;
    // 该哈希表已有节点的数量
    unsigned long used;
} dictht;

//字典类型特定函数,保存了一簇用于操作特定类型键值对的函数
typedef struct dictType
{
    // 计算哈希值的函数
    unsigned int (*hashFunction)(const void *key);
    // 复制键的函数
    void *(*keyDup)(void *privdata, const void *key);
    // 复制值的函数
    void *(*valDup)(void *privdata, const void *obj);
    // 对比键的函数
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    // 销毁键的函数
    void (*keyDestructor)(void *privdata, void *key);
    // 销毁值的函数
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

//字典
typedef struct dict
{
    // 类型特定函数,Redis 会为用途不同的字典设置不同的类型特定函数。
    dictType *type;
    // 私有数据,保存了需要传给那些类型特定函数的可选参数
    void *privdata;
    // 哈希表,每个字典都使用两个哈希表，从而实现渐进式 rehash
    dictht ht[2];
    // rehash 索引,当 rehash 不在进行时，值为 -1
    int rehashidx;
    // 目前正在运行的安全迭代器的数量,字典有安全迭代器的情况下不能进行 rehash
    int iterators;
} dict;

//字典的迭代器
typedef struct dictIterator
{
    // 被迭代的字典
    dict *d;
    // table ：正在被迭代的哈希表号码，值可以是 0 或 1 。
    // index ：迭代器当前所指向的哈希表索引位置。
    // safe ：标识这个迭代器是否安全,为1表示安全
    // 如果 safe 属性的值为 1 ，那么在迭代进行的过程中，程序仍然可以执行 dictAdd 、 dictFind 和其他函数，对字典进行修改。
    // 如果 safe 不为 1 ，那么程序只会调用 dictNext 对字典进行迭代，而不对字典进行修改。
    int table, index, safe;

    // entry ：当前迭代到的节点的指针
    // nextEntry ：当前迭代节点的下一个节点
    //             因为在安全迭代器运作时， entry 所指向的节点可能会被修改，
    //             所以需要一个额外的指针来保存下一节点的位置，
    //             从而防止指针丢失
    dictEntry *entry, *nextEntry;
    // 用于验证使用迭代器使用前后字典是否发生了改变
    long long fingerprint;
} dictIterator;

//下面7个函数都是调用 dictType结构里面的函数

// 释放给定字典节点的值，entry的类型是dictEntry*
#define dictFreeVal(d, entry)     \
    if ((d)->type->valDestructor) \
    (d)->type->valDestructor((d)->privdata, (entry)->v.val)

// 设置给定字典节点的值
#define dictSetVal(d, entry, _val_)                                 \
    do                                                              \
    {                                                               \
        if ((d)->type->valDup)                                      \
            entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
        else                                                        \
            entry->v.val = (_val_);                                 \
    } while (0)

// 将一个有符号整数设为节点的值
#define dictSetSignedIntegerVal(entry, _val_) \
    do                                        \
    {                                         \
        entry->v.s64 = _val_;                 \
    } while (0)

// 将一个无符号整数设为节点的值
#define dictSetUnsignedIntegerVal(entry, _val_) \
    do                                          \
    {                                           \
        entry->v.u64 = _val_;                   \
    } while (0)

// 释放给定字典节点的键
#define dictFreeKey(d, entry)     \
    if ((d)->type->keyDestructor) \
    (d)->type->keyDestructor((d)->privdata, (entry)->key)

// 设置给定字典节点的键
#define dictSetKey(d, entry, _key_)                               \
    do                                                            \
    {                                                             \
        if ((d)->type->keyDup)                                    \
            entry->key = (d)->type->keyDup((d)->privdata, _key_); \
        else                                                      \
            entry->key = (_key_);                                 \
    } while (0)

// 比对两个键
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? (d)->type->keyCompare((d)->privdata, key1, key2) : (key1) == (key2))

// d的类型是dict,he的类型是dictEntry*，ht的类型是dictht*
// 计算给定键的哈希值
#define dictHashKey(d, key) (d)->type->hashFunction(key)
// 返回获取给定节点的键
#define dictGetKey(he) ((he)->key)
// 返回获取给定节点的值
#define dictGetVal(he) ((he)->v.val)
// 返回获取给定节点的有符号整数值
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
// 返回给定节点的无符号整数值
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
// 返回给定字典的大小，两个哈希表都要统计
#define dictSlots(d) ((d)->ht[0].size + (d)->ht[1].size)
// 返回字典的已有节点数量
#define dictSize(d) ((d)->ht[0].used + (d)->ht[1].used)
// 查看字典是否正在 rehash
#define dictIsRehashing(ht) ((ht)->rehashidx != -1)

// 创建新的字典
dict *dictCreate(dictType *type, void *privDataPtr);
// 手动扩展字典，使其大小为第一个大于等于 size 的 2 的 N 次方
int dictExpand(dict *d, unsigned long size);
// 给定字典,让它的已用节点数和字典大小之间的比率接近 1:1，且小于1：1
int dictResize(dict *d);
// 尝试将给定键值对添加到字典中，只有给定键 key 不存在于字典时，添加操作才会成功,成功返回1失败返回0
int dictAdd(dict *d, void *key, void *val);
// 将给定的键值对添加到字典里面， 如果键已经存在于字典，那么用新值取代原有的值。键是新加的返回1，取代的返回0
int dictReplace(dict *d, void *key, void *val);
// 从字典中删除包含给定键的节点，并且调用键值的释放函数来删除键值
// 找到并成功删除返回 DICT_OK ，没找到则返回 DICT_ERR
int dictDelete(dict *d, const void *key);
// 不调用键值的释放函数来删除键值
int dictDeleteNoFree(dict *d, const void *key);
// 删除并释放整个字典
void dictRelease(dict *d);
// 清空字典上的所有哈希表节点，并重置字典属性，不释放字典
void dictEmpty(dict *d, void(callback)(void *));

// 返回字典中包含键 key 的节点，找到返回节点，找不到返回 NULL
dictEntry *dictFind(dict *d, const void *key);
// 获取包含给定键的节点的值,如果节点不为空，返回节点的值,否则返回 NULL
void *dictFetchValue(dict *d, const void *key);

// 开启自动 rehash
void dictEnableResize(void);
// 关闭自动 rehash
void dictDisableResize(void);
// 执行 N 步渐进式 rehash，返回 1 表示仍有键需要从 0 号哈希表移动到 1 号哈希表，返回 0 则表示所有键都已经迁移完毕。
int dictRehash(dict *d, int n);
// 在给定毫秒数内，以 100 步为单位，对字典进行 rehash
int dictRehashMilliseconds(dict *d, int ms);

//生成一个64位的fingerprint，可以认为每个字典的fingerprint都是独一无二的
//在使用不安全的迭代器时，使用前后检查fingerprint是否发生了变化，如果变了则说明中途出现了非法的操作，导致字典发生了改变
long long dictFingerprint(dict *d);
//创建并返回给定字典的不安全迭代器
dictIterator *dictGetIterator(dict *d);
//创建并返回给定字典的安全迭代器
dictIterator *dictGetSafeIterator(dict *d);
//返回迭代器指向的当前节点，并会往后迭代，字典迭代完毕时，返回 NULL
dictEntry *dictNext(dictIterator *iter);
//释放给定字典迭代器
void dictReleaseIterator(dictIterator *iter);
//遍历字典的函数
typedef void(dictScanFunction)(void *privdata, const dictEntry *de);
/* 迭代给定字典中的元素，每个元素都用函数fn处理一次
 * 使用这个函数的方法如下：
    1）一开始，你使用 0 作为游标来调用函数，即把v设为0
    2) 函数执行一步迭代操作，并返回一个下次迭代时使用的新游标。
    3) 当函数返回的游标为 0 时，迭代完成。
 * 在迭代从开始到结束期间，一直存在于字典的元素肯定会被迭代到， 但一个元素可能会被返回多次
 * 一次迭代会返回多个元素（同一个桶中的）
*/
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

//返回任意一个节点
dictEntry *dictGetRandomKey(dict *d);
//返回任意count个节点
int dictGetRandomKeys(dict *d, dictEntry **des, int count);

//哈希函数种子的类型是unsigned int
//设置哈希函数的种子
void dictSetHashFunctionSeed(unsigned int initval);
//获得哈希函数的种子
unsigned int dictGetHashFunctionSeed(void);
// 用于整数的哈希函数
unsigned int dictIntHashFunction(unsigned int key);
// 直接返回原值得哈希函数
unsigned int dictIdentityHashFunction(unsigned int key);
// MurmurHash2哈希函数
unsigned int dictGenHashFunction(const void *key, int len);
//另一种哈希函数
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);

// void dictPrintStats(dict *d);

// extern dictType dictTypeHeapStringCopyKey;
// extern dictType dictTypeHeapStrings;
// extern dictType dictTypeHeapStringCopyKeyValue;

#endif