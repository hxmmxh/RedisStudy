#ifndef HXM_OBJECT_H
#define HXM_OBJECT_H

#include <stdlib.h>

// Least Recently Used，和时间有关的宏和声明
#define REDIS_LRU_BITS 24                               // 表示时间的无符号整数的位数
#define REDIS_LRU_CLOCK_MAX ((1 << REDIS_LRU_BITS) - 1) // 时间的最大值
#define REDIS_LRU_CLOCK_RESOLUTION 1000                 // 时间的分辨率，单位为ms
#define LRU_CLOCK() getLRUClock()                       // 以后还需要修改
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

#define REDIS_HEAD 0
#define REDIS_TAIL 1

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
    robj *minstring, *maxstring;
    robj *integers[REDIS_SHARED_INTEGERS]; //共享的 REDIS_ENCODING_INT编码的对象
};
// 共享对象
extern struct sharedObjects shared;
//创建共享对象，在服务器初始化时会调用
void createSharedObjects(void);

// 创建一个新的 robj 对象
robj *createObject(int type, void *ptr);

// 检查对象 o 的类型是否和 type 相同：相同返回 0  不相同返回 1 ，并向客户端回复一个错误
int checkType(/*redisClient *c, */robj *o, int type);


// 为对象的引用计数增一
void incrRefCount(robj *o);
// 为对象的引用计数减一,当对象的引用计数降为 0 时，释放对象
void decrRefCount(robj *o);
// 作用于特定数据结构的释放函数包装，用于传入一个需要void free_object(void*)类型的函数
void decrRefCountVoid(void *o);
// 将对象的引用计数设为 0 ，但并不释放对象, 在将一个对象传入一个会增加引用计数的函数中时，非常有用
robj *resetRefCount(robj *obj);


// 返回编码的字符串表示
char *strEncoding(int encoding);
// 使用近似 LRU 算法，计算出给定对象的闲置时长,单位为ms
unsigned long long estimateObjectIdleTime(robj *o);


// OBJECT 命令的辅助函数，用于在不修改 LRU 时间的情况下，尝试获取 key 对象
// robj *objectCommandLookup(/*redisClient *c,*/ robj *key);
// robj *objectCommandLookupOrReply
// void objectCommand(redisClient *c);

#endif