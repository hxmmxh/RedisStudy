#ifndef HXM_CLIENT_H
#define HXM_CLIENT_H

typedef struct redisClient
{

    int fd;      // 套接字描述符
    redisDb *db; // 当前正在使用的数据库
    int dictid;  // 当前正在使用的数据库的 id （号码）

    robj *name; // 客户端的名字

    // 在服务器将客户端发送的命令请求保存到客户端状态的 querybuf 属性之后， 服务器将对命令请求的内容进行分析， 并将得出的命令参数以及命令参数的个数分别保存到客户端状态的 argv 属性和 argc 属性
    // 服务器将根据项 argv[0] 的值， 在命令表中查找命令所对应的命令实现函数。将客户端状态的 cmd 指针指向这个结构
    sds querybuf;             // 输入缓冲区
    size_t querybuf_peak;     // 查询缓冲区长度峰值
    int argc;                 // 命令参数数量
    robj **argv;              // 命令参数对象数组
    struct redisCommand *cmd; //命令的实现函数
    struct redisCommand *lastcmd;

    // 请求的类型：内联命令还是多条命令
    int reqtype;

    // 剩余未读取的命令内容数量
    int multibulklen;

    // 命令内容的长度
    long bulklen;

    // 输出缓冲区
    char buf[REDIS_REPLY_CHUNK_BYTES]; // 固定大小的回复缓冲区
    int bufpos;                        // 回复偏移量，buf数组中已经使用的字节量
    list *reply;                       // 可变大小的输出缓冲区，回复链表
    unsigned long reply_bytes;         // 回复链表中对象的总大小

    // 已发送字节，处理 short write 用
    int sentlen; /* Amount of bytes already sent in the current
                               buffer or object being sent. */

    time_t ctime;                        // 创建客户端的时间
    time_t lastinteraction;              // 客户端最后一次和服务器互动的时间
    time_t obuf_soft_limit_reached_time; // 客户端的输出缓冲区第一次超过软性限制的时间

    // 客户端状态标志
    int flags; /* REDIS_SLAVE | REDIS_MONITOR | REDIS_MULTI ... */

    // 当 server.requirepass 不为 NULL 时 代表认证的状态
    // 0 代表未认证， 1 代表已认证
    int authenticated;

    // 复制状态
    int replstate; /* replication state if this is a slave */
    // 用于保存主服务器传来的 RDB 文件的文件描述符
    int repldbfd; /* replication DB file descriptor */

    // 读取主服务器传来的 RDB 文件的偏移量
    off_t repldboff; /* replication DB file offset */
    // 主服务器传来的 RDB 文件的大小
    off_t repldbsize; /* replication DB file size */

    sds replpreamble; /* replication DB preamble. */

    // 主服务器的复制偏移量
    long long reploff; /* replication offset if this is our master */
    // 从服务器最后一次发送 REPLCONF ACK 时的偏移量
    long long repl_ack_off; /* replication ack offset, if this is a slave */
    // 从服务器最后一次发送 REPLCONF ACK 的时间
    long long repl_ack_time; /* replication ack time, if this is a slave */
    // 主服务器的 master run ID
    // 保存在客户端，用于执行部分重同步
    char replrunid[REDIS_RUN_ID_SIZE + 1]; /* master run id if this is a master */
    // 从服务器的监听端口号
    int slave_listening_port; /* As configured with: SLAVECONF listening-port */

    // 事务状态
    multiState mstate; /* MULTI/EXEC state */

    // 阻塞类型
    int btype; /* Type of blocking op if REDIS_BLOCKED. */
    // 阻塞状态
    blockingState bpop; /* blocking state */

    // 最后被写入的全局复制偏移量
    long long woff; /* Last write global replication offset. */

    // 被监视的键
    list *watched_keys; /* Keys WATCHED for MULTI/EXEC CAS */

    // 这个字典记录了客户端所有订阅的频道
    // 键为频道名字，值为 NULL
    // 也即是，一个频道的集合
    dict *pubsub_channels; /* channels a client is interested in (SUBSCRIBE) */

    // 链表，包含多个 pubsubPattern 结构
    // 记录了所有订阅频道的客户端的信息
    // 新 pubsubPattern 结构总是被添加到表尾
    list *pubsub_patterns; /* patterns a client is interested in (SUBSCRIBE) */
    sds peerid;            /* Cached peer ID. */

} redisClient;

#endif