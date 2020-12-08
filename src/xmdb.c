#include "xmdb.h"
#include <assert.h>

int removeExpire(redisDb *db, robj *key)
{
    // 在键空间中确保键存在
    assert(dictFind(db->dict, key->ptr) != NULL);
    // 在过期字典中删除过期时间
    return dictDelete(db->expires, key->ptr) == DICT_OK;
}

void setExpire(redisDb *db, robj *key, long long when)
{
    dictEntry *kde, *de;
    // 取出键
    kde = dictFind(db->dict, key->ptr);
    // 根据键取出键的过期时间
    de = dictReplaceRaw(db->expires, dictGetKey(kde));
    // 设置键的过期时间
    // 这里是直接使用整数值来保存过期时间，不是用 INT 编码的 String 对象
    dictSetSignedIntegerVal(de, when);
}

long long getExpire(redisDb *db, robj *key)
{
    dictEntry *de;

    // 获取键的过期时间
    // 如果过期时间不存在，那么直接返回
    if (dictSize(db->expires) == 0 ||
        (de = dictFind(db->expires, key->ptr)) == NULL)
        return -1;
    assert(dictFind(db->dict, key->ptr) != NULL);

    // 返回过期时间
    return dictGetSignedIntegerVal(de);
}

void propagateExpire(redisDb *db, robj *key)
{
    /*
    robj *argv[2];

    // 构造一个 DEL key 命令
    argv[0] = shared.del;
    argv[1] = key;
    incrRefCount(argv[0]);
    incrRefCount(argv[1]);

    // 传播到 AOF
    if (server.aof_state != REDIS_AOF_OFF)
        feedAppendOnlyFile(server.delCommand, db->id, argv, 2);

    // 传播到所有附属节点
    replicationFeedSlaves(server.slaves, db->id, argv, 2);

    decrRefCount(argv[0]);
    decrRefCount(argv[1]);
    */
}

int expireIfNeeded(redisDb *db, robj *key)
{

    // 取出键的过期时间
    mstime_t when = getExpire(db, key);
    mstime_t now;

    // 没有过期时间
    if (when < 0)
        return 0;

    // 如果服务器正在进行载入，那么不进行任何过期检查
    if (server.loading)
        return 0;

    // 如果处于Lua脚本环境下，我们认为在脚本启动的那一刻，时间已经停止了，
    // 所以如果在脚本环境下，现在的时间就是脚本启动时的时间
    now = server.lua_caller ? server.lua_time_start : mstime();

    // 当服务器作为一个从服务器运行在复制模式时，立刻返回
    // 附属节点并不主动删除 key，它只返回一个逻辑上正确的返回值
    // 真正的删除操作要等待主节点发来删除命令时才执行，从而保证数据的同步
    // 即 虽然返回1，但实际上并没有删除节点
    if (server.masterhost != NULL)
        return now > when;

    // 运行到这里，表示键带有过期时间，并且服务器为主节点
    // 如果未过期，返回 0
    if (now <= when)
        return 0;

    server.stat_expiredkeys++;

    // 向 AOF 文件和附属节点传播过期信息
    propagateExpire(db, key);

    // 发送事件通知
    //notifyKeyspaceEvent(REDIS_NOTIFY_EXPIRED,"expired", key, db->id);

    // 将过期键从数据库中删除
    return dbDelete(db, key);
}

robj *lookupKey(redisDb *db, robj *key)
{

    // 查找键空间
    dictEntry *de = dictFind(db->dict, key->ptr);
    // 节点存在
    if (de)
    {
        // 取出值
        robj *val = dictGetVal(de);
        // 更新时间信息（只在不存在子进程时执行，防止破坏 copy-on-write 机制）
        if (server.rdb_child_pid == -1 && server.aof_child_pid == -1)
            val->lru = LRU_CLOCK();
        return val;
    }
    else
    {
        return NULL;
    }
}

robj *lookupKeyRead(redisDb *db, robj *key)
{
    robj *val;
    // 检查 key 释放已经过期
    expireIfNeeded(db, key);
    // 从数据库中取出键的值
    val = lookupKey(db, key);
    // 更新命中/不命中信息
    if (val == NULL)
        server.stat_keyspace_misses++;
    else
        server.stat_keyspace_hits++;
    // 返回值
    return val;
}

robj *lookupKeyWrite(redisDb *db, robj *key)
{
    // 删除过期键
    expireIfNeeded(db, key);
    // 查找并返回 key 的值对象
    return lookupKey(db, key);
}

robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply)
{
    // 查找
    robj *o = lookupKeyRead(c->db, key);
    // 决定是否发送信息
    if (!o)
        addReply(c, reply);
    return o;
}

robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply)
{
    robj *o = lookupKeyWrite(c->db, key);
    if (!o)
        addReply(c, reply);
    return o;
}

void dbAdd(redisDb *db, robj *key, robj *val)
{
    // 复制键名
    sds copy = sdsdup(key->ptr);
    // 尝试添加键值对
    int retval = dictAdd(db->dict, copy, val);
    // 如果键已经存在，那么停止
    assert(retval == REDIS_OK);

    // 如果开启了集群模式，那么将键保存到槽里面
    //if (server.cluster_enabled)
    //slotToKeyAdd(key);
}

void dbOverwrite(redisDb *db, robj *key, robj *val)
{
    dictEntry *de = dictFind(db->dict, key->ptr);
    // 节点必须存在，否则中止
    assert(de != NULL);
    // 覆写旧值
    dictReplace(db->dict, key->ptr, val);
}

void setKey(redisDb *db, robj *key, robj *val)
{

    // 添加或覆写数据库中的键值对
    if (lookupKeyWrite(db, key) == NULL)
    {
        dbAdd(db, key, val);
    }
    else
    {
        dbOverwrite(db, key, val);
    }
    incrRefCount(val);
    // 移除键的过期时间
    removeExpire(db, key);
    // 发送键修改通知
    // signalModifiedKey(db, key);
}

int dbExists(redisDb *db, robj *key)
{
    return dictFind(db->dict, key->ptr) != NULL;
}

robj *dbRandomKey(redisDb *db)
{
    dictEntry *de;

    while (1)
    {
        sds key;
        robj *keyobj;

        // 从键空间中随机取出一个键节点
        de = dictGetRandomKey(db->dict);

        // 数据库为空
        if (de == NULL)
            return NULL;

        // 取出键
        key = dictGetKey(de);
        // 为键创建一个字符串对象，对象的值为键的名字
        keyobj = createStringObject(key, sdslen(key));
        // 检查键是否带有过期时间
        if (dictFind(db->expires, key))
        {
            // 如果键已经过期，那么将它删除，并继续随机下个键
            if (expireIfNeeded(db, keyobj))
            {
                decrRefCount(keyobj);
                continue;
            }
        }
        // 返回被随机到的键（的名字）
        return keyobj;
    }
}

int dbDelete(redisDb *db, robj *key)
{
    // 删除键的过期时间
    if (dictSize(db->expires) > 0)
        dictDelete(db->expires, key->ptr);
    // 删除键值对
    if (dictDelete(db->dict, key->ptr) == DICT_OK)
    {
        // 如果开启了集群模式，那么从槽中删除给定的键
        //if (server.cluster_enabled)
        //slotToKeyDel(key);
        return 1;
    }
    else
    {
        // 键不存在
        return 0;
    }
}

robj *dbUnshareStringValue(redisDb *db, robj *key, robj *o)
{
    assert(o->type == REDIS_STRING);
    if (o->refcount != 1 || o->encoding != REDIS_ENCODING_RAW)
    {
        robj *decoded = getDecodedObject(o);
        o = createRawStringObject(decoded->ptr, sdslen(decoded->ptr));
        decrRefCount(decoded);
        dbOverwrite(db, key, o);
    }
    return o;
}

long long emptyDb(void(callback)(void *))
{
    int j;
    long long removed = 0;
    // 清空所有数据库
    for (j = 0; j < server.dbnum; j++)
    {
        // 记录被删除键的数量
        removed += dictSize(server.db[j].dict);
        // 删除所有键值对
        dictEmpty(server.db[j].dict, callback);
        // 删除所有键的过期时间
        dictEmpty(server.db[j].expires, callback);
    }

    // 如果开启了集群模式，那么还要移除槽记录
    //if (server.cluster_enabled)
    //slotToKeyFlush();

    // 返回键的数量
    return removed;
}

int selectDb(redisClient *c, int id)
{

    // 确保 id 在正确范围内
    if (id < 0 || id >= server.dbnum)
        return REDIS_ERR;

    // 切换数据库（更新指针）
    c->db = &server.db[id];

    return REDIS_OK;
}