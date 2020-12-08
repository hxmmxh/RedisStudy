#ifndef HXM_SERVER_H
#define HXM_SERVER_H

// 压缩列表可用于列表，哈希，有序集合对象的底层编码，但都存在大小和数量的限制
#define REDIS_HASH_MAX_ZIPLIST_ENTRIES 512
#define REDIS_HASH_MAX_ZIPLIST_VALUE 64

#define REDIS_LIST_MAX_ZIPLIST_ENTRIES 512
#define REDIS_LIST_MAX_ZIPLIST_VALUE 64

#define REDIS_ZSET_MAX_ZIPLIST_ENTRIES 128
#define REDIS_ZSET_MAX_ZIPLIST_VALUE 64

// 同样的整数集合作为集合对象的底层编码时也存在数量的限制
#define REDIS_SET_MAX_INTSET_ENTRIES 512

#include <sys/types.h>

#include "stdlib.h"

#include "xmdict.h"
#include "xmadlist.h"

#include "xmdb.h"

extern struct redisServer server;

struct redisClient;

struct redisServer
{
    // 编码的边界条件
    size_t hash_max_ziplist_entries;
    size_t hash_max_ziplist_value;
    size_t list_max_ziplist_entries;
    size_t list_max_ziplist_value;
    size_t zset_max_ziplist_entries;
    size_t zset_max_ziplist_value;
    size_t set_max_intset_entries;

    // 成功查找键的次数
    long long stat_keyspace_hits;
    // 查找键失败的次数
    long long stat_keyspace_misses;

    // 已过期的键数量
    long long stat_expiredkeys;

    /*******************数据库**********************************/
    // 数据库数组的大小
    int dbnum;
    // 数据库
    redisDb *db;

    /******RDB或AOF持久化相关的标志*************************************************/

    // 负责执行 BGSAVE 的子进程的 ID， 没在执行 BGSAVE 时，设为 -1
    pid_t rdb_child_pid;
    // 负责进行 AOF 重写的子进程 ID，没在执行 AOF重写 时，设为 -1
    pid_t aof_child_pid;
    // 这个值为真时，表示服务器正在进行载入
    int loading;




    /******************发布与订阅**************************************************/
    // 字典，键为频道，值为链表
    // 链表中保存了所有订阅某个频道的客户端
    // 新客户端总是被添加到链表的表尾
    dict *pubsub_channels; 
    // 这个链表记录了客户端订阅的所有模式的名字
    list *pubsub_patterns;  
    // 可以被发布的通知的类型
    int notify_keyspace_events; 



    /***********Lua脚本相关*************************************/

    // 当前正在执行 EVAL 命令的客户端，如果没有就是 NULL
    redisClient *lua_caller;
    // 脚本开始执行的时间
    mstime_t lua_time_start;

    /************复制相关************************/
    // 主服务器的验证密码
    char *masterauth;
    // 主服务器的地址
    char *masterhost;
    // 主服务器的端口
    int masterport;
};

#endif