#ifndef HXM_RIO_H
#define HXM_RIO_H

#include <stdio.h>
#include <stdint.h>
#include "xmsds.h"

// RIO 是一个可以面向流、可用于对多种不同的输入（目前是文件和内存字节）进行编程的抽象
struct _rio
{
    // 读写函数，返回零表示失败，不为0表示成功
    // 这两个函数不接受部分写入/读书，只要成功就说明buf里的数据都读/写了
    size_t (*read)(struct _rio *, void *buf, size_t len);
    size_t (*write)(struct _rio *, const void *buf, size_t len);
    // 偏移量
    off_t (*tell)(struct _rio *);

    // 校验和计算函数，每次有写入/读取新数据时都要计算一次
    void (*update_cksum)(struct _rio *, const void *buf, size_t len);

    // 当前校验和
    uint64_t cksum;
    // 读或写的总字节数
    size_t processed_bytes;
    // 单次读或写的最大字节数
    size_t max_processing_chunk;

    union
    {
        // 如果是写/读一个字符串缓冲
        struct
        {
            // 缓存指针
            sds ptr;
            // 偏移量
            off_t pos;
        } buffer;
        // 如果是写/读一个文件
        struct
        {
            // 被打开文件的指针
            FILE *fp;
            // 最近一次 fsync() 以来，写入的字节量
            off_t buffered;
            // 写入多少字节之后，会自动执行一次 fsync()
            // 默认情况下， bytes 被设为 0 ，表示不执行自动 fsync
            // 为了防止一次写入过多内容而设置的。
            // 通过显示地、间隔性地调用 fsync可以将写入的 I/O 压力分担到多次 fsync 调用中。
            off_t autosync;
        } file;
    } io;
};

typedef struct _rio rio;

// 将 buf 中的 len 字节写入到 r 中。
// 写入成功返回1，写入失败返回 -1 。
static inline size_t rioWrite(rio *r, const void *buf, size_t len)
{
    while (len)
    {
        // 每次写入的字节数
        size_t bytes_to_write = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        // 更新校验和
        if (r->update_cksum)
            r->update_cksum(r, buf, bytes_to_write);
        // 写入
        if (r->write(r, buf, bytes_to_write) == 0)
            return 0;
        // 更新buf
        buf = (char *)buf + bytes_to_write;
        len -= bytes_to_write;
        r->processed_bytes += bytes_to_write;
    }
    return 1;
}

// 从 r 中读取 len 字节，并将内容保存到 buf 中。
// 读取成功返回 1 ，失败返回 0
static inline size_t rioRead(rio *r, void *buf, size_t len)
{
    while (len)
    {
        size_t bytes_to_read = (r->max_processing_chunk && r->max_processing_chunk < len) ? r->max_processing_chunk : len;
        if (r->read(r, buf, bytes_to_read) == 0)
            return 0;
        if (r->update_cksum)
            r->update_cksum(r, buf, bytes_to_read);
        buf = (char *)buf + bytes_to_read;
        len -= bytes_to_read;
        r->processed_bytes += bytes_to_read;
    }
    return 1;
}

// 返回 r 的当前偏移量。
static inline off_t rioTell(rio *r)
{
    return r->tell(r);
}

// 初始化文件流
void rioInitWithFile(rio *r, FILE *fp);
// 初始化内存流
void rioInitWithBuffer(rio *r, sds s);

// 以带 '\r\n' 后缀的形式写入字符串表示的 count 到 RIO
// “prefix+count+\r\n”
// 成功返回写入的数量，失败返回 0 。
size_t rioWriteBulkCount(rio *r, char prefix, int count);
// 写入字符串
// "$+len+\r\n+buf+\r\n"
size_t rioWriteBulkString(rio *r, const char *buf, size_t len);
// 将longlong以字符串的格式写入
size_t rioWriteBulkLongLong(rio *r, long long l);
// 将double以字符串的格式写入
size_t rioWriteBulkDouble(rio *r, double d);

// crc校验和计算函数
uint64_t crc64(uint64_t crc, const unsigned char *s, uint64_t l);
// 通用校验和计算函数
void rioGenericUpdateChecksum(rio *r, const void *buf, size_t len);
// 设置写文件时的autosync
void rioSetAutoSync(rio *r, off_t bytes);

#endif