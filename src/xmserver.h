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

extern struct redisServer server;

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

    // 负责执行 BGSAVE 的子进程的 ID
    // 没在执行 BGSAVE 时，设为 -1
    pid_t rdb_child_pid;
    // 负责进行 AOF 重写的子进程 ID
    // 没在执行 AOF重写 时，设为 -1
    pid_t aof_child_pid;
};

#endif