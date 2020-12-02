#include "xmredis.h"
#include "xmobject.h"
#include "xmsds.h"

#include <stdlib.h>
#include <stdio.h>

// 创建字符串对象，编码可能是raw，也可能是embstr
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
// 释放字符串对象
void freeStringObject(robj *o);

// 检查对象 o 中的值能否表示为 long long 类型
// 可以则返回 REDIS_OK ，并将 long long 值保存到 *llval 中。不可以则返回 REDIS_ERR
int isObjectRepresentableAsLongLong(robj *o, long long *llval);

// 判断是否是字符编码
#define sdsEncodedObject(objptr) \
    (objptr->encoding == REDIS_ENCODING_RAW || objptr->encoding == REDIS_ENCODING_EMBSTR)

// 尝试对字符串对象进行编码，以节约内存。
//会将可以编码成整数的字符串对象进行编码，会将符合条件的raw转换成embstr,并且还会清除字符串对象的free空间
robj *tryObjectEncoding(robj *o);
// 解码对象，将对象的值从整数转换为字符串,返回一个输入对象的解码版本（RAW 编码）
// 每次通过这个函数给一个变量赋值时，记得在使用完后对该变量减一引用次数
robj *getDecodedObject(robj *o);


//对比两个字符串对象
int compareStringObjects(robj *a, robj *b);
// 用strcoll函数对比两个字符串对象
int collateStringObjects(robj *a, robj *b);
/* 如果两个对象的值在字符串的形式上相等，那么返回 1 ， 否则返回 0 。
比 (compareStringObject(a, b) == 0) 更快一些*/
int equalStringObjects(robj *a, robj *b);
// 返回字符串对象中字符串值的长度
size_t stringObjectLen(robj *o);

// 尝试从对象中取出 double 值
// 转换成功则将值保存在 *target 中，函数返回 REDIS_OK, 否则，函数返回 REDIS_ERR
int getDoubleFromObject(robj *o, double *target); 
// 如果尝试失败的话，就返回指定的回复 msg 给客户端，函数返回 REDIS_ERR
int getDoubleFromObjectOrReply(/*redisClient *c,*/ robj *o, double *target, const char *msg);
// 尝试从对象中取出 long double 值
int getLongDoubleFromObject(robj *o, long double *target);
int getLongDoubleFromObjectOrReply(/*redisClient *c,*/ robj *o, long double *target, const char *msg);
//  尝试从对象 o 中取出整数值，或者尝试将对象 o 所保存的值转换为整数值，并将这个整数值保存到 *target 中
int getLongLongFromObject(robj *o, long long *target);
int getLongLongFromObjectOrReply(/*redisClient *c,*/robj *o, long long *target, const char *msg);
int getLongFromObjectOrReply(/*redisClient *c, */robj *o, long *target, const char *msg);