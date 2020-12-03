#ifndef HXM_DB_H
#define HXM_DB_H

#include "xmdict.h"
#include "xmobject.h"
#include "xmsds.h"

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

int removeExpire(redisDb *db, robj *key);
void propagateExpire(redisDb *db, robj *key);
int expireIfNeeded(redisDb *db, robj *key);
long long getExpire(redisDb *db, robj *key);
void setExpire(redisDb *db, robj *key, long long when);
robj *lookupKey(redisDb *db, robj *key);
robj *lookupKeyRead(redisDb *db, robj *key);
robj *lookupKeyWrite(redisDb *db, robj *key);
robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply);
robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply);
void dbAdd(redisDb *db, robj *key, robj *val);
void dbOverwrite(redisDb *db, robj *key, robj *val);
void setKey(redisDb *db, robj *key, robj *val);
int dbExists(redisDb *db, robj *key);
robj *dbRandomKey(redisDb *db);
int dbDelete(redisDb *db, robj *key);
robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o);
long long emptyDb(void(callback)(void *));
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
