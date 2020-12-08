#ifndef HXM_DB_H
#define HXM_DB_H

#include "xmdict.h"
#include "xmobject.h"
#include "xmsds.h"

#include "xmredis.h"
#include "xmserver.h"
#include "xmclient.h"

/* To improve the quality of the LRU approximation we take a set of keys
 * that are good candidate for eviction across freeMemoryIfNeeded() calls.
 *
 * Entries inside the eviciton pool are taken ordered by idle time, putting
 * greater idle times to the right (ascending order).
 *
 * Empty entries have the key pointer set to NULL. */
// 用于调用freeMemoryIfNeeded() 时提高近似估计的有效率
#define REDIS_EVICTION_POOL_SIZE 16
struct evictionPoolEntry
{
    // 键的闲散时间
    unsigned long long idle; 
    // 键名
    sds key; 
};

typedef struct redisDb
{
    // 数据库键空间，保存着数据库中的所有键值对
    dict *dict;
    // 键的过期时间，字典的键为键，字典的值为过期事件 UNIX 时间戳
    struct dict *expires;
    // 正处于阻塞状态的键
    struct dict *blocking_keys;
    // 可以解除阻塞的键
    struct dict *ready_keys;
    // 正在被 WATCH 命令监视的键
    struct dict *watched_keys;
    // 里面的节点按照闲散时间增序排序
    struct evictionPoolEntry *eviction_pool; 
    // 数据库号码
    int id;
    // 数据库的键的平均 TTL ，统计信息
    long long avg_ttl;
} redisDb;


// 和过期时间相关的函数
// 移除键 key 的过期时间，成功返回1，键原本就没有过期时间返回0
int removeExpire(redisDb *db, robj *key);
// 将键 key 的过期时间设为 when，绝对时间
void setExpire(redisDb *db, robj *key, long long when);
// 返回给定 key 的过期时间。 如果键没有设置过期时间，那么返回 -1 。绝对时间
long long getExpire(redisDb *db, robj *key);
// 将过期时间传播到附属节点和 AOF 文件。
// 当一个键在主节点中过期时，主节点会向所有附属节点和 AOF 文件传播一个显式的 DEL 命令。
void propagateExpire(redisDb *db, robj *key);
// 检查 key 是否已经过期，如果是的话，将它从数据库中删除。
// 返回 0 表示键没有过期时间，或者键未过期。返回 1 表示键已经因为过期而被删除了。
int expireIfNeeded(redisDb *db, robj *key);



// 从数据库 db 中取出键 key 的值（对象,如果 key 的值存在，那么返回该值；否则，返回 NUL
robj *lookupKey(redisDb *db, robj *key);
// 为执行读取操作而取出键 key 在数据库 db 中的值。并根据是否成功找到值，更新服务器的命中/不命中信息。
robj *lookupKeyRead(redisDb *db, robj *key);
// 为执行写入操作而取出键 key 在数据库 db 中的值。不会更新服务器的命中/不命中信息。
robj *lookupKeyWrite(redisDb *db, robj *key);
// key 不存在，那么向客户端发送 reply 参数中的信息，并返回 NULL 
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply);
// key 不存在，那么向客户端发送 reply 参数中的信息，并返回 NULL 
robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply);


// 尝试将键值对 key 和 val 添加到数据库中。调用者负责对 key 和 val 的引用计数进行增加。
// 程序在键已经存在时会停止。
void dbAdd(redisDb *db, robj *key, robj *val);
// 为已存在的键关联一个新值。调用者负责对新值 val 的引用计数进行增加。
// 这个函数不会修改键的过期时间。
// 如果键不存在，那么函数停止
void dbOverwrite(redisDb *db, robj *key, robj *val);
// 高层次的 SET 操作函数。可以在不管键 key 是否存在的情况下，将它和 val 关联起来。
// 值对象的引用计数会被增加
// 监视键 key 的客户端会收到键已经被修改的通知
// 键的过期时间会被移除（键变为持久的）
void setKey(redisDb *db, robj *key, robj *val);
// 检查键 key 是否存在于数据库中，存在返回 1 ，不存在返回 0 。
int dbExists(redisDb *db, robj *key);
// 随机从数据库中取出一个键，并以字符串对象的方式返回这个键。如果数据库为空，那么返回 NULL 
// 这个函数保证被返回的键都是未过期的。
robj *dbRandomKey(redisDb *db);
//  从数据库中删除给定的键，键的值，以及键的过期时间。
// 删除成功返回 1 ，因为键不存在而导致删除失败时，返回 0 。
int dbDelete(redisDb *db, robj *key);
// 把不可修改的 共享对象和embstr/int编码的字符串变成可以修改的raw编码字符串
// 同时把键key的值设为修改后的对象，并返回它
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o);
// 清空服务器的所有数据,返回删除的键的数量，每删除65536个键，调用callback
long long emptyDb(void(callback)(void *));

// 将客户端的目标数据库切换为 id 所指定的数据库，成功返回REDIS_OK，失败返回REDIS_ERR
int selectDb(redisClient *c, int id);
void signalModifiedKey(redisDb *db, robj *key);
void signalFlushedDb(int dbid);
unsigned int getKeysInSlot(unsigned int hashslot, robj **keys, unsigned int count);
unsigned int countKeysInSlot(unsigned int hashslot);
unsigned int delKeysInSlot(unsigned int hashslot);
int verifyClusterConfigWithData(void);
void scanGenericCommand(redisClient *c, robj *o, unsigned long cursor);
int parseScanCursorOrReply(redisClient *c, robj *o, unsigned long *cursor);

#endif
