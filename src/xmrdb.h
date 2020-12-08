#ifndef HXM_RDB_H
#define HXM_RDB_H

#include "xmrio.h"
#include "xmobject.h"



/*
`<REDIS><db_version><database><EOF><check_sum>`
--5字节--|--4字节---|----------|1字节|---8字节---|
database: `<SELECTDB><db_number><key_value_pairs>`
           --1字节---|-1/2/5字节-|---------------
key_value_pairs: `<TYPE><key><value>`或者`<EXPIRETIME_MS><ms><TYPE><key><value>`
                  -1字节-|---|----|       ---1字节------|8字节|1字节|----|-----
*/

// RDB 的版本
#define REDIS_RDB_VERSION 6

/*
 * 通过读取第一字节的最高 2 位来判断长度,用于分析db_number 
 * db_number 保存着一个数据库号码，根据号码的大小不同， 这个部分的长度可以是 1 字节、 2 字节或者 5 字节
 * 00|000000 => 长度编码在这一字节的其余 6 位中
 *
 * 01|000000 00000000 => 长度为 14 位，当前字节 6 位，加上下个字节 8 位
 *
 * 10|000000 [32 bit integer] => 长度由后跟的 32 位保存
 *
 * 11|000000 后跟一个特殊编码的对象。字节中的 6 位指定对象的类型。
 *           查看 REDIS_RDB_ENC_* 定义获得更多消息
 *
 * 一个字节（的其中 6 个字节）可以保存的最大长度是 63 （包括在内），
 * 对于大多数键和值来说，都已经足够了。
 */
#define REDIS_RDB_6BITLEN 0
#define REDIS_RDB_14BITLEN 1
#define REDIS_RDB_32BITLEN 2
#define REDIS_RDB_ENCVAL 3

// 表示读取/写入错误
#define REDIS_RDB_LENERR UINT_MAX

/* 当对象是一个字符串对象时，
 * 最高两个位之后的两个位（第 3 个位和第 4 个位）指定了对象的特殊编码*/
#define REDIS_RDB_ENC_INT8 0  // 8位整数
#define REDIS_RDB_ENC_INT16 1 // 16位整数
#define REDIS_RDB_ENC_INT32 2 // 32位整数
#define REDIS_RDB_ENC_LZF 3   // 被LZF 算法压缩后保存的字符串

// 对象的类型，TYPE属性
#define REDIS_RDB_TYPE_STRING 0
#define REDIS_RDB_TYPE_LIST 1
#define REDIS_RDB_TYPE_SET 2
#define REDIS_RDB_TYPE_ZSET 3
#define REDIS_RDB_TYPE_HASH 4
#define REDIS_RDB_TYPE_LIST_ZIPLIST 10
#define REDIS_RDB_TYPE_SET_INTSET 11
#define REDIS_RDB_TYPE_ZSET_ZIPLIST 12
#define REDIS_RDB_TYPE_HASH_ZIPLIST 13

// 检查给定类型是否对象
#define rdbIsObjectType(t) ((t >= 0 && t <= 4) || (t >= 10 && t <= 13))

// RDB文件中的特殊操作标识符
// 以 MS 计算的过期时间
#define REDIS_RDB_OPCODE_EXPIRETIME_MS 252
// 以秒计算的过期时间
#define REDIS_RDB_OPCODE_EXPIRETIME 253
// 选择数据库
#define REDIS_RDB_OPCODE_SELECTDB 254
// 数据库的结尾（但不是 RDB 文件的结尾），结尾是校验和
#define REDIS_RDB_OPCODE_EOF 255


// 将长度为 1 字节的字符 type 写入到 rdb 文件中。
int rdbSaveType(rio *rdb, unsigned char type);
// 从 rdb 中载入 1 字节长的 type 数据
// 可以用于载入键的类型（REDIS_RDB_TYPE_*）
// 也可以用于载入特殊标识号（REDIS_RDB_OPCODE_*）
int rdbLoadType(rio *rdb);

// 将长度为 8 字节的毫秒过期时间写入到 rdb 中
int rdbSaveMillisecondTime(rio *rdb, long long t);
// 从 rdb 中载入 8 字节长的毫秒过期时间
long long rdbLoadMillisecondTime(rio *rdb);
// 载入以秒为单位的过期时间，长度为 4 字节
time_t rdbLoadTime(rio *rdb);

// 对 长度值len 进行特殊编码之后写入到 rdb 。写入成功返回保存编码后的 len 所需的字节数。
// 用第一字节的最高 2 位来判断长度，REDIS_RDB_nBITLEN类型的
int rdbSaveLen(rio *rdb, uint32_t len);
// 读入一个被编码的长度值。 
// 如果 length 值不是整数，而是一个被编码后值，即REDIS_RDB_ENCVAL，那么 isencoded 将被设为 1
uint32_t rdbLoadLen(rio *rdb, int *isencoded);







int rdbSaveObjectType(rio *rdb, robj *o);
int rdbLoadObjectType(rio *rdb);
int rdbLoad(char *filename);
int rdbSaveBackground(char *filename);
void rdbRemoveTempFile(pid_t childpid);
int rdbSave(char *filename);
int rdbSaveObject(rio *rdb, robj *o);
off_t rdbSavedObjectLen(robj *o);
off_t rdbSavedObjectPages(robj *o);
robj *rdbLoadObject(int type, rio *rdb);
void backgroundSaveDoneHandler(int exitcode, int bysignal);
int rdbSaveKeyValuePair(rio *rdb, robj *key, robj *val, long long expiretime, long long now);
robj *rdbLoadStringObject(rio *rdb);



#endif
