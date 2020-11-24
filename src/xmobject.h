#ifndef HXM_OBJECT_H
#define HXM_OBJECT_H

#include <stdlib.h>

#include "xmsds.h"

// Least Recently Used，和时间有关的宏和声明
#define REDIS_LRU_BITS 24                               //表示时间的无符号整数的位数
#define REDIS_LRU_CLOCK_MAX ((1 << REDIS_LRU_BITS) - 1) //时间的最大值
#define REDIS_LRU_CLOCK_RESOLUTION 1000                 //时间的分辨率，单位为ms
#define LRU_CLOCK() getLRUClock()                       //
unsigned int getLRUClock(void);

// 对象结构的声明
typedef struct redisObject
{
    unsigned type : 4;             // 类型
    unsigned encoding : 4;         // 编码
    unsigned lru : REDIS_LRU_BITS; // 对象最后一次被访问的时间
    int refcount;                  // 引用计数
    void *ptr;                     // 指向实际值的指针
} robj;

// 对象类型
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_ZSET 3
#define REDIS_HASH 4

// 编码方式
#define REDIS_ENCODING_RAW 0        //简单动态字符串
#define REDIS_ENCODING_INT 1        //long 类型的整数
#define REDIS_ENCODING_HT 2         //字典
#define REDIS_ENCODING_ZIPMAP 3     //已被废弃
#define REDIS_ENCODING_LINKEDLIST 4 //双端链表
#define REDIS_ENCODING_ZIPLIST 5    //压缩列表
#define REDIS_ENCODING_INTSET 6     //整数集合
#define REDIS_ENCODING_SKIPLIST 7   //跳跃表和字典
#define REDIS_ENCODING_EMBSTR 8     //embstr 编码的简单动态字符串

//共享对象
#define REDIS_SHARED_INTEGERS 10000
struct sharedObjects
{
    robj *integers[REDIS_SHARED_INTEGERS]; //共享的 REDIS_ENCODING_INT编码的对象
};
//创建共享对象，在服务器初始化时会调用
void createSharedObjects(void);

// 创建一个新的 robj 对象
robj *createObject(int type, void *ptr);
// 创建字符串对象
robj *createStringObject(char *ptr, size_t len);
// 创建一个 REDIS_ENCODING_RAW 编码的字符对象
robj *createRawStringObject(char *ptr, size_t len);
// 创建一个 REDIS_ENCODING_EMBSTR 编码的字符对象
robj *createEmbeddedStringObject(char *ptr, size_t len);
// 根据传入的整数值，创建一个字符串对象，底层编码是不定的
robj *createStringObjectFromLongLong(long long value);
// 根据传入的 long double 值，为它创建一个字符串对象，底层编码是不定的
robj *createStringObjectFromLongDouble(long double value);
/* 复制一个字符串对象，复制出的对象和输入对象拥有相同编码。输出对象的 refcount 总为 1 
 * 在复制一个包含整数值的字符串对象时，总是产生一个非共享的对象*/
robj *dupStringObject(robj *o);

int isObjectRepresentableAsLongLong(robj *o, long long *llongval);






robj *createListObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);

void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);

robj *tryObjectEncoding(robj *o);
robj *getDecodedObject(robj *o);
size_t stringObjectLen(robj *o);

// 为对象的引用计数增一
void incrRefCount(robj *o);
// 为对象的引用计数减一,当对象的引用计数降为 0 时，释放对象
void decrRefCount(robj *o);
// 作用于特定数据结构的释放函数包装
void decrRefCountVoid(void *o);
// 将对象的引用计数设为 0 ，但并不释放对象, 在将一个对象传入一个会增加引用计数的函数中时，非常有用
robj *resetRefCount(robj *obj);

#endif