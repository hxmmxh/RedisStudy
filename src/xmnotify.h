#ifndef HXM_NOTIFY_H
#define HXM_NOTIFY_H

#include "xmsds.h"
#include "xmobject.h"
#include "xmserver.h"

#define REDIS_NOTIFY_KEYSPACE (1<<0)    // K 键空间通知
#define REDIS_NOTIFY_KEYEVENT (1<<1)    // E 键时间通知
#define REDIS_NOTIFY_GENERIC (1<<2)     // g 类型无关的通用命令的通知，例如DEL,EXPIRE等
#define REDIS_NOTIFY_STRING (1<<3)      // $ 字符串命令的通知
#define REDIS_NOTIFY_LIST (1<<4)        // l 列表命令的通知
#define REDIS_NOTIFY_SET (1<<5)         // s 集合命令的通知
#define REDIS_NOTIFY_HASH (1<<6)        // h 哈希命令的通知
#define REDIS_NOTIFY_ZSET (1<<7)        // z 有序集合命令的通知
#define REDIS_NOTIFY_EXPIRED (1<<8)     // x 过期事件，每当有过期键被删除时发送
#define REDIS_NOTIFY_EVICTED (1<<9)     // e 驱逐事件，每当有键因为maxmemory政策而被删除时发送
// A 所有类型通知
#define REDIS_NOTIFY_ALL (REDIS_NOTIFY_GENERIC | REDIS_NOTIFY_STRING | REDIS_NOTIFY_LIST | REDIS_NOTIFY_SET | REDIS_NOTIFY_HASH | REDIS_NOTIFY_ZSET | REDIS_NOTIFY_EXPIRED | REDIS_NOTIFY_EVICTED)      


// 对传入的字符串参数进行分析， 给出相应的 flags 值
// 如果传入的字符串中有不能识别的字符串，那么返回 -1 
int keyspaceEventsStringToFlags(char *classes);
// 根据 flags 值还原设置这个 flags 所需的字符串
sds keyspaceEventsFlagsToString(int flags);
// 发送通知
// type表示通知的类型，event 参数是一个字符串表示的事件名, 
// key 参数是一个 Redis 对象表示的键名, dbid 参数为键所在的数据库
void notifyKeyspaceEvent(int type, char *event, robj *key, int dbid);

#endif