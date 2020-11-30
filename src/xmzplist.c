#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "xmmalloc.h"
#include "xmzplist.h"
#include "xmmalloc.h"
#include "xmendianconv.h"

//zlend的特殊值 0xFF
#define ZIP_END 255
// previous_entry_length的分界线，小于254为一字节，大于等于254为5字节
#define ZIP_BIGLEN 254

// 字符串编码和整数编码的掩码
#define ZIP_STR_MASK 0xc0 // 11000000，字符串编码的长度不看前两位
#define ZIP_INT_MASK 0x30 // 00110000，整数编码的长度不看前四位

// 字符串编码类型,b代表一个字节，-代表为空
#define ZIP_STR_06B (0 << 6) // 00000000 1 字节	长度小于等于 63 字节的字节数组
#define ZIP_STR_14B (1 << 6) // 01000000 b  2 字节	长度小于等于 16383 字节的字节数组。
#define ZIP_STR_32B (2 << 6) // 10------ b b b b 5 字节	长度小于等于 4294967295 的字节数组。

// 整数编码类型
#define ZIP_INT_16B (0xc0 | 0 << 4) // 11000000 1 字节	int16_t 类型的整数。
#define ZIP_INT_32B (0xc0 | 1 << 4) // 11010000	1 字节	int32_t 类型的整数。
#define ZIP_INT_64B (0xc0 | 2 << 4) // 11100000	1 字节	int64_t 类型的整数。
#define ZIP_INT_24B (0xc0 | 3 << 4) // 11110000	1 字节	24 位有符号整数。
#define ZIP_INT_8B 0xfe             // 11111110	1 字节	8 位有符号整数。

// 4 位整数编码的掩码和类型，保存的范围为0-12
#define ZIP_INT_IMM_MASK 0x0f                     // 00001111
#define ZIP_INT_IMM_MIN 0xf1                      // 最小值 11110001 ，但是保存整数0
#define ZIP_INT_IMM_MAX 0xfd                      // 最大值11111101，保存着12
#define ZIP_INT_IMM_VAL(v) (v & ZIP_INT_IMM_MASK) //取出四位整数值

// 24 位整数的最大值和最小值
#define INT24_MAX 0x7fffff
#define INT24_MIN (-INT24_MAX - 1)

// 查看给定编码 enc 是否字符串编码
#define ZIP_IS_STR(enc) (((enc)&ZIP_STR_MASK) < ZIP_STR_MASK)

// ziplist 属性宏，zl的类型是unsigned char *
// 定位到 ziplist 的 bytes 属性，该属性记录了整个 ziplist 所占用的内存字节数
// 用于取出 bytes 属性的现有值，或者为 bytes 属性赋予新值
#define ZIPLIST_BYTES(zl) (*((uint32_t *)(zl)))
// 定位到 ziplist 的 offset 属性，该属性记录了到达表尾节点的偏移量
// 用于取出 offset 属性的现有值，或者为 offset 属性赋予新值
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t *)((zl) + sizeof(uint32_t))))
// 定位到 ziplist 的 length 属性，该属性记录了 ziplist 包含的节点数量
// 用于取出 length 属性的现有值，或者为 length 属性赋予新值
#define ZIPLIST_LENGTH(zl) (*((uint16_t *)((zl) + sizeof(uint32_t) * 2)))
// 返回 ziplist 表头的大小
#define ZIPLIST_HEADER_SIZE (sizeof(uint32_t) * 2 + sizeof(uint16_t))

// 返回指向 ziplist 第一个节点（的起始位置）的指针
#define ZIPLIST_ENTRY_HEAD(zl) ((zl) + ZIPLIST_HEADER_SIZE)
// 返回指向 ziplist 最后一个节点（的起始位置）的指针
#define ZIPLIST_ENTRY_TAIL(zl) ((zl) + intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// 返回指向 ziplist 末端 ZIP_END （的起始位置）的指针，-1是往前一字节，也就是uint8_t
#define ZIPLIST_ENTRY_END(zl) ((zl) + intrev32ifbe(ZIPLIST_BYTES(zl)) - 1)

// 增加 ziplist 的节点数，只有长度小于65535时才能增加
// c标准规定无后缀的整型字面值如果能用int型表示，那类型就是int型
// 在这里宏里展开的数字都是int类型，不用担心运算结果大于65535时的溢出问题
// 不过增加长度时，每次只会增加1个节点，也可以不用考虑溢出
#define ZIPLIST_INCR_LENGTH(zl, incr)                                                   \
    {                                                                                   \
        if (ZIPLIST_LENGTH(zl) < UINT16_MAX)                                            \
            ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl)) + incr); \
    }

// 保存 ziplist 节点信息的结构
typedef struct zlentry
{
    // prevrawlen ：前置节点的长度
    // prevrawlensize ：编码 prevrawlen 所需的字节大小
    unsigned int prevrawlensize, prevrawlen;
    // len ：当前节点值的长度
    // lensize ：编码 len 所需的字节大小
    unsigned int lensize, len;
    // 当前节点 header 的大小
    // 等于 prevrawlensize + lensize
    unsigned int headersize;
    // 当前节点值所使用的编码类型
    unsigned char encoding;
    // 指向当前节点的指针
    unsigned char *p;
} zlentry;

//从 ptr 中取出节点值的编码类型，并将它保存到 encoding 变量中。
//如果是字符串编码，只需记录前两位就知道字符串的长度
//而如果是整数编码，需要整个字节才能知道具体的整数类型
#define ZIP_ENTRY_ENCODING(ptr, encoding) \
    do                                    \
    {                                     \
        (encoding) = (ptr[0]);            \
        if ((encoding) < ZIP_STR_MASK)    \
            (encoding) &= ZIP_STR_MASK;   \
    } while (0)

// 返回整数编码时整数数据的字节数
static unsigned int zipIntSize(unsigned char encoding)
{
    switch (encoding)
    {
    case ZIP_INT_8B:
        return 1;
    case ZIP_INT_16B:
        return 2;
    case ZIP_INT_24B:
        return 3;
    case ZIP_INT_32B:
        return 4;
    case ZIP_INT_64B:
        return 8;
    // 四位整数，没有content空间保存戴护具
    default:
        return 0;
    }
    return 0;
}

// rawlen是encoding的初始值
// 返回encoding具体的字节数，可能是1，2，5
// p不为空的话，把encoding也写入到p中
static unsigned int zipEncodeLength(unsigned char *p, unsigned char encoding, unsigned int rawlen)
{
    unsigned char len = 1, buf[5];

    // 如果是字符串编码
    if (ZIP_IS_STR(encoding))
    {
        /* Although encoding is given it may not be set for strings,
         * so we determine it here using the raw length. */
        // 00111111，1 字节	长度小于等于 63 字节的字节数组
        if (rawlen <= 0x3f)
        {
            if (!p)
                return len;
            buf[0] = ZIP_STR_06B | rawlen;
        }
        // 00111111 11111111 2 字节 长度小于等于 16383 字节的字节数组。
        else if (rawlen <= 0x3fff)
        {
            len += 1;
            if (!p)
                return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        }
        else
        {
            len += 4;
            if (!p)
                return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = (rawlen >> 24) & 0xff;
            buf[2] = (rawlen >> 16) & 0xff;
            buf[3] = (rawlen >> 8) & 0xff;
            buf[4] = rawlen & 0xff;
        }
    }
    // 整数编码
    else
    {
        // 字节数永远为1
        if (!p)
            return len;
        buf[0] = encoding;
    }

    // 将编码后的长度写入 p
    memcpy(p, buf, len);
    // 返回编码所需的字节数
    return len;
}

/* 解码 ptr 指针，ptr指针指向的是encoding的开头，取出列表节点的相关信息，并将它们保存在以下变量中：
 * - encoding 保存节点值的编码类型。
 * - lensize 保存编码节点长度所需的字节数。
 * - len 编码记录的节点的长度。
 */
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len)       \
    do                                                       \
    {                                                        \
        /* 取出值的编码类型 */                       \
        ZIP_ENTRY_ENCODING((ptr), (encoding));               \
        /* 字符串编码 */                                \
        if ((encoding) < ZIP_STR_MASK)                       \
        {                                                    \
            if ((encoding) == ZIP_STR_06B)                   \
            {                                                \
                (lensize) = 1;                               \
                (len) = (ptr)[0] & 0x3f;                     \
            }                                                \
            else if ((encoding) == ZIP_STR_14B)              \
            {                                                \
                (lensize) = 2;                               \
                (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1]; \
            }                                                \
            else if (encoding == ZIP_STR_32B)                \
            {                                                \
                (lensize) = 5;                               \
                (len) = ((ptr)[1] << 24) |                   \
                        ((ptr)[2] << 16) |                   \
                        ((ptr)[3] << 8) |                    \
                        ((ptr)[4]);                          \
            }                                                \
            else                                             \
            {                                                \
                assert(NULL);                                \
            }                                                \
        }                                                    \
        /* 整数编码 */                                   \
        else                                                 \
        {                                                    \
            (lensize) = 1;                                   \
            (len) = zipIntSize(encoding);                    \
        }                                                    \
    } while (0)

/* len->ptr
 * 对前置节点的长度 len 进行编码，并将它写入到 p 中，
 * 然后返回编码 len 所需的字节数量。
 * 如果 p 为 NULL ，那么不进行写入，仅返回编码 len 所需的字节数量。
 */
static unsigned int zipPrevEncodeLength(unsigned char *p, unsigned int len)
{

    // 仅返回编码 len 所需的字节数量
    if (p == NULL)
    {
        // 如果小于254则是1字节，否则是5字节
        return (len < ZIP_BIGLEN) ? 1 : sizeof(len) + 1;
    }
    // 写入并返回编码 len 所需的字节数量
    else
    {
        // 1 字节
        if (len < ZIP_BIGLEN)
        {
            p[0] = len;
            return 1;
        }
        // 5 字节
        else
        {
            //第一个字节不记录长度，用于标识
            p[0] = ZIP_BIGLEN;
            // 写入编码
            memcpy(p + 1, &len, sizeof(len));
            // 如果有必要的话，进行大小端转换
            memrev32ifbe(p + 1);
            // 返回编码长度
            return 1 + sizeof(len);
        }
    }
}

// 将前置节点长度 len 编码至一个 5 字节长的 header 中。
static void zipPrevEncodeLengthForceLarge(unsigned char *p, unsigned int len)
{
    if (p == NULL)
        return;
    // 设置 5 字节长度标识
    p[0] = ZIP_BIGLEN;
    // 写入 len
    memcpy(p + 1, &len, sizeof(len));
    memrev32ifbe(p + 1);
}

// 解码 ptr 指针，取出编码前置节点长度所需的字节数，并将它保存到 prevlensize 变量中。
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) \
    do                                           \
    {                                            \
        if ((ptr)[0] < ZIP_BIGLEN)               \
        {                                        \
            (prevlensize) = 1;                   \
        }                                        \
        else                                     \
        {                                        \
            (prevlensize) = 5;                   \
        }                                        \
    } while (0)

/* ptr->len
*解码 ptr 指针，
*取出编码前置节点长度所需的字节数，
*并将这个字节数保存到 prevlensize 中。
*然后根据 prevlensize ，从 ptr 中取出前置节点的长度值，
*并将这个长度值保存到 prevlen 变量中。*/
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen)    \
    do                                                   \
    {                                                    \
        /* 先计算被编码长度值的字节数 */    \
        ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);        \
        /* 再根据编码字节数来取出长度值 */ \
        if ((prevlensize) == 1)                          \
        {                                                \
            (prevlen) = (ptr)[0];                        \
        }                                                \
        else if ((prevlensize) == 5)                     \
        {                                                \
            assert(sizeof((prevlensize)) == 4);          \
            memcpy(&(prevlen), ((char *)(ptr)) + 1, 4);  \
            memrev32ifbe(&prevlen);                      \
        }                                                \
    } while (0)

//计算节点长度 len 所需的长度编码字节数，
//减去p指向的节点的前置节点长度
static int zipPrevLenByteDiff(unsigned char *p, unsigned int len)
{
    unsigned int prevlensize;
    // 取出编码原来的前置节点长度所需的字节数
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    // 计算编码 len 所需的字节数，然后进行减法运算
    return zipPrevEncodeLength(NULL, len) - prevlensize;
}

// 返回指针 p 所指向的节点占用的字节数总和。
static unsigned int zipRawEntryLength(unsigned char *p)
{
    unsigned int prevlensize, encoding, lensize, len;
    // 取出编码前置节点的长度所需的字节数
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    // 取出当前节点值的编码类型，编码节点值长度所需的字节数，以及节点值的长度
    ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
    // 计算节点占用的字节数总和
    return prevlensize + lensize + len;
}

/* 检查s中指向的字符串能否被编码为整数。
 * 如果可以的话，将编码后的整数保存在指针 v 的值中，并将编码的方式保存在指针 encoding 的值中。
 * 解析成功返回1，失败返回0 
 */
static int zipTryEncoding(unsigned char *s, unsigned int slen, long long *v, unsigned char *encoding)
{
    long long value;
    // 忽略太长或太短的字符串,为什么是32？？
    if (slen >= 32 || slen == 0)
        return 0;
    // 尝试转换
    if (string2ll((char *)s, slen, &value))
    {
        // 转换成功，以从小到大的顺序检查适合值 value 的编码方式
        if (value >= 0 && value <= 12)
        {
            // 1+value，所以编码的范围是11110001到1111
            *encoding = ZIP_INT_IMM_MIN + value;
        }
        else if (value >= INT8_MIN && value <= INT8_MAX)
        {
            *encoding = ZIP_INT_8B;
        }
        else if (value >= INT16_MIN && value <= INT16_MAX)
        {
            *encoding = ZIP_INT_16B;
        }
        else if (value >= INT24_MIN && value <= INT24_MAX)
        {
            *encoding = ZIP_INT_24B;
        }
        else if (value >= INT32_MIN && value <= INT32_MAX)
        {
            *encoding = ZIP_INT_32B;
        }
        else
        {
            *encoding = ZIP_INT_64B;
        }
        // 记录值到指针
        *v = value;
        // 返回转换成功标识
        return 1;
    }
    // 转换失败
    return 0;
}

// 以encoding 指定的编码方式，将整数值 value 写入到 p 。
static void zipSaveInteger(unsigned char *p, int64_t value, unsigned char encoding)
{
    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B)
    {
        ((int8_t *)p)[0] = (int8_t)value;
    }
    else if (encoding == ZIP_INT_16B)
    {
        i16 = value;
        memcpy(p, &i16, sizeof(i16));
        memrev16ifbe(p);
    }
    else if (encoding == ZIP_INT_24B)
    {
        // 24位的整数，左移8位，当成32位整数处理
        i32 = value << 8;
        memrev32ifbe(&i32);
        memcpy(p, ((uint8_t *)&i32) + 1, sizeof(i32) - sizeof(uint8_t));
    }
    else if (encoding == ZIP_INT_32B)
    {
        i32 = value;
        memcpy(p, &i32, sizeof(i32));
        memrev32ifbe(p);
    }
    else if (encoding == ZIP_INT_64B)
    {
        i64 = value;
        memcpy(p, &i64, sizeof(i64));
        memrev64ifbe(p);
    }
    else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
    {
        // 4位整数不需要额外的content空间
    }
    else
    {
        assert(NULL);
    }
}

// 以 encoding 指定的编码方式，读取并返回指针 p 中的整数值。
static int64_t zipLoadInteger(unsigned char *p, unsigned char encoding)
{
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;

    if (encoding == ZIP_INT_8B)
    {
        ret = ((int8_t *)p)[0];
    }
    else if (encoding == ZIP_INT_16B)
    {
        memcpy(&i16, p, sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    }
    else if (encoding == ZIP_INT_32B)
    {
        memcpy(&i32, p, sizeof(i32));
        memrev32ifbe(&i32);
        ret = i32;
    }
    else if (encoding == ZIP_INT_24B)
    {
        i32 = 0;
        // 00 xx xx xx
        memcpy(((uint8_t *)&i32) + 1, p, sizeof(i32) - sizeof(uint8_t));
        // xx xx xx 00
        memrev32ifbe(&i32);
        // xx xx xx
        ret = i32 >> 8;
    }
    else if (encoding == ZIP_INT_64B)
    {
        memcpy(&i64, p, sizeof(i64));
        memrev64ifbe(&i64);
        ret = i64;
    }
    else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
    {
        // 4位整数需要-1
        ret = (encoding & ZIP_INT_IMM_MASK) - 1;
    }
    else
    {
        assert(NULL);
    }

    return ret;
}

// 将 p 所指向的列表节点的信息全部保存到 zlentry 中，并返回该 zlentry 。
static zlentry zipEntry(unsigned char *p)
{
    zlentry e;
    // e.prevrawlensize 保存着编码前一个节点的长度所需的字节数
    // e.prevrawlen 保存着前一个节点的长度
    ZIP_DECODE_PREVLEN(p, e.prevrawlensize, e.prevrawlen);

    // p + e.prevrawlensize 将指针移动到列表节点本身
    // e.encoding 保存着节点值的编码类型
    // e.lensize 保存着编码节点值长度所需的字节数
    // e.len 保存着节点值的长度
    ZIP_DECODE_LENGTH(p + e.prevrawlensize, e.encoding, e.lensize, e.len);

    // 计算头结点的字节数
    e.headersize = e.prevrawlensize + e.lensize;

    // 记录指针
    e.p = p;

    return e;
}

unsigned char *ziplistNew(void)
{
    // ZIPLIST_HEADER_SIZE 是 ziplist 表头的大小
    // 1 字节是表末端 ZIP_END 的大小
    unsigned int bytes = ZIPLIST_HEADER_SIZE + 1;
    // 为表头和表末端分配空间
    unsigned char *zl = zmalloc(bytes);
    // 初始化表属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);                     //初始值为11
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE); //初始值为10
    ZIPLIST_LENGTH(zl) = 0;
    // 设置表末端
    zl[bytes - 1] = ZIP_END;
    return zl;
}

//调整 ziplist 的大小为 len 字节。
//当 ziplist 原有的大小小于 len 时，扩展 ziplist 不会改变 ziplist 原有的元素。
static unsigned char *ziplistResize(unsigned char *zl, unsigned int len)
{
    // 用 zrealloc ，扩展时不改变现有元素
    zl = zrealloc(zl, len);
    // 更新 bytes 属性
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    // 重新设置表末端
    zl[len - 1] = ZIP_END;
    return zl;
}

// 完成连锁更新操作
// 程序的检查是针对 p 的后续节点，而不是 p 所指向的节点。因为节点 p 在传入之前已经完成了所需的空间扩展工作。
// 在插入操作时，p是插入节点的后一个节点
// 在删除操作时，p是被删除的节点的后一个节点
static unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p)
{
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char *np;
    zlentry cur, next;

    while (p[0] != ZIP_END)
    {
        // 将 p 所指向的节点的信息保存到 cur 结构中
        cur = zipEntry(p);
        // 当前节点的长度
        rawlen = cur.headersize + cur.len;
        // 计算编码当前节点的长度所需的字节数
        rawlensize = zipPrevEncodeLength(NULL, rawlen);

        // 如果已经没有后续空间需要更新了，跳出
        // 一个有效节点的开头一定不会是0XFF
        if (p[rawlen] == ZIP_END)
            break;

        // 取出后续节点的信息，保存到 next 结构中
        next = zipEntry(p + rawlen);

        // 后续节点编码当前节点的空间已经足够，无须再进行任何处理，跳出
        // 可以证明，只要遇到一个空间足够的节点，
        // 那么这个节点之后的所有节点的空间都是足够的
        if (next.prevrawlen == rawlen)
            break;

        if (next.prevrawlensize < rawlensize)
        {
            // 执行到这里，表示 next 空间的大小不足以编码 cur 的长度
            // 所以程序需要对 next 节点的（header 部分）空间进行扩展
            // 记录 p 的偏移量
            offset = p - zl;
            // 计算需要增加的节点数量
            extra = rawlensize - next.prevrawlensize;
            // 扩展 zl 的大小
            zl = ziplistResize(zl, curlen + extra);
            // 还原指针 p
            p = zl + offset;

            // 记录下一节点的偏移量
            np = p + rawlen;
            noffset = np - zl;

            // 当 next 节点不是表尾节点时，更新列表到表尾节点的偏移量
            //
            // 不用更新的情况（next 为表尾节点)，只是next的空间增加了，next之间的布局没有发生变化：
            //
            // |     | next |      ==>    |     | new next          |
            //       ^                          ^
            //       |                          |
            //     tail                        tail
            //
            // 需要更新的情况（next 不是表尾节点），到表尾节点的偏移量需要增加：
            //
            // | next |     |   ==>     | new next          |     |
            //        ^                        ^
            //        |                        |
            //    old tail                 old tail
            //
            // 更新之后：
            //
            // | new next          |     |
            //                     ^
            //                     |
            //                  new tail
            // T = O(1)
            if ((zl + intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np)
            {
                ZIPLIST_TAIL_OFFSET(zl) =
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + extra);
            }

            // 向后移动 cur 节点之后的数据，为 cur 的新 header 腾出空间
            //
            // 示例：
            //
            // | header | value |  ==>  | header |    | value |  ==>  | header      | value |
            //                                   |<-->|
            //                            为新 header 腾出的空间
            // T = O(N)
            // 目的地：np + rawlensize，np+新的
            // 源地址：np节点初始时encoding的起始地址。prelen之后的地址
            memmove(np + rawlensize,
                    np + next.prevrawlensize,
                    curlen - noffset - next.prevrawlensize - 1);
            // 将新的前一节点长度值编码进新的 next 节点的 header
            zipPrevEncodeLength(np, rawlen);
            // 移动指针，继续处理下个节点
            p += rawlen;
            // 更新整个压缩链表的字节长度
            curlen += extra;
        }
        else
        {
            if (next.prevrawlensize > rawlensize)
            {
                // 执行到这里，说明 next 节点编码前置节点的 header 空间有 5 字节
                // 而编码 rawlen 只需要 1 字节
                // 但是程序不会对 next 进行缩小，
                // 所以这里只将 rawlen 写入 5 字节的 header 中就算了。
                zipPrevEncodeLengthForceLarge(p + rawlen, rawlen);
            }
            else
            {
                // 运行到这里，
                // 说明 cur 节点的长度正好可以编码到 next 节点的 header 中
                zipPrevEncodeLength(p + rawlen, rawlen);
            }
            break;
        }
    }
    return zl;
}

// 从位置 p 开始，连续删除 num 个节点。
// 函数的返回值为处理删除操作之后的 ziplist 。
static unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num)
{
    unsigned int i, totlen, deleted = 0;
    size_t offset;
    int nextdiff = 0;
    zlentry first, tail;

    // 计算被删除节点总共占用的内存字节数
    // 以及被删除节点的总个数
    first = zipEntry(p);
    for (i = 0; p[0] != ZIP_END && i < num; i++)
    {
        p += zipRawEntryLength(p);
        deleted++;
    }
    // p现在指向最后一个要删除节点的下一个节点开头
    // totlen 是所有被删除节点总共占用的内存字节数
    totlen = p - first.p;
    if (totlen > 0)
    {
        if (p[0] != ZIP_END)
        {
            // 执行这里，表示被删除节点之后仍然有节点存在
            // 因为位于被删除范围之后的第一个节点的 header 部分的大小
            // 可能容纳不了新的前置节点，所以需要计算新旧前置节点之间的字节数差
            nextdiff = zipPrevLenByteDiff(p, first.prevrawlen);
            // 如果有需要的话，将指针 p 后退 nextdiff 字节，为新 header 空出空间
            p -= nextdiff;
            // 将 first 的前置节点的长度编码至 p 中
            zipPrevEncodeLength(p, first.prevrawlen);

            // 如果p现在就是最后一个节点的话，到达表尾的偏移量和nextdiff无关
            // p的大小不影响到表尾的偏移量
            // 更新到达表尾的偏移量
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) - totlen);

            // 如果被删除节点之后，有多于一个节点
            // 那么程序需要将 nextdiff 记录的字节数也计算到表尾偏移量中，
            // 这样才能让表尾偏移量正确对齐表尾节点
            // 因为上面的操作相当于给某个中间节点加了nextdiff的长度
            tail = zipEntry(p);
            if (p[tail.headersize + tail.len] != ZIP_END)
            {
                ZIPLIST_TAIL_OFFSET(zl) =
                    intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + nextdiff);
            }
            // 从表尾向表头移动数据，覆盖被删除节点的数据
            memmove(first.p, p,
                    intrev32ifbe(ZIPLIST_BYTES(zl)) - (p - zl) - 1);
        }
        else
        {
            // 执行这里，表示被删除节点之后已经没有其他节点了
            // 压缩列表现在的最后一个节点应该是被删除的第一个节点的前一个节点
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe((first.p - zl) - first.prevrawlen);
        }

        offset = first.p - zl;
        // 缩小并更新 ziplist 的长度
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl)) - totlen + nextdiff);
        // 更新ziplist的节点数
        ZIPLIST_INCR_LENGTH(zl, -deleted);
        p = zl + offset;

        // 如果 p 所指向的节点的大小已经变更，那么进行级联更新
        // 检查 p 之后的所有节点是否符合 ziplist 的编码要求
        if (nextdiff != 0)
            zl = __ziplistCascadeUpdate(zl, p);
    }
    return zl;
}

// 根据指针 p 所指定的位置，将长度为 slen 的字符串 s 插入到 zl 中。
static unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
    // 记录当前 ziplist 的长度
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen, prevlen = 0;
    size_t offset;
    int nextdiff = 0;
    unsigned char encoding = 0;
    // 做个初始化，防止警告，因为有些情况下用不到这个value
    long long value = 123456789;
    zlentry entry, tail;

    if (p[0] != ZIP_END)
    {
        // 如果 p[0] 不指向列表末端，说明列表非空，并且 p 正指向列表的其中一个节点
        // 那么取出 p 所指向节点的信息，并将它保存到 entry 结构中
        // 然后用 prevlen 变量记录前置节点的长度
        // （当插入新节点之后 p的前置节点就成了新节点的前置节点）
        entry = zipEntry(p);
        prevlen = entry.prevrawlen;
    }
    else
    {
        // 如果 p 指向表尾末端，那么程序需要检查列表是否为：
        // 1)如果 ptail 也指向 ZIP_END ，那么列表为空；
        // 2)如果列表不为空，那么 ptail 将指向列表的最后一个节点。
        unsigned char *ptail = ZIPLIST_ENTRY_TAIL(zl);
        if (ptail[0] != ZIP_END)
        {
            // 表尾节点为新节点的前置节点
            // 取出表尾节点的长度
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 尝试看能否将输入字符串转换为整数，如果成功的话：
    // 1)value 将保存转换后的整数值
    // 2)encoding 则保存适用于 value 的编码方式
    // 无论使用什么编码，reqlen 都保存节点值的长度
    if (zipTryEncoding(s, slen, &value, &encoding))
    {
        reqlen = zipIntSize(encoding);
    }
    else
    {
        // 如果不能转化成整数，reqlen 就等于字符串的长度
        reqlen = slen;
    }

    // 计算编码前置节点的长度所需的大小
    reqlen += zipPrevEncodeLength(NULL, prevlen);
    // 计算编码当前节点值所需的大小
    // 到了这一步，reqlen就是新插入的节点占的总的大小
    reqlen += zipEncodeLength(NULL, encoding, slen);

    // 只要新节点不是被添加到列表末端，
    // 那么程序就需要检查看 p 所指向的节点（的 header）能否编码新节点的长度。
    // nextdiff 保存了新旧编码之间的字节大小差，如果这个值大于 0
    // 那么说明需要对 p 所指向的节点（的 header ）进行扩展
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;

    // 因为重分配空间可能会改变 zl 的地址
    // 所以在分配之前，需要记录 zl 到 p 的偏移量，然后在分配之后依靠偏移量还原 p
    offset = p - zl;
    // curlen 是 ziplist 原来的长度
    // reqlen 是整个新节点的长度
    // nextdiff 是新节点的后继节点扩展 header 的长度（要么 0 字节，要么 4 个字节）
    zl = ziplistResize(zl, curlen + reqlen + nextdiff);
    p = zl + offset;

    if (p[0] != ZIP_END)
    {
        // 新元素之后还有节点，因为新元素的加入，需要对这些原有节点进行调整
        // 移动现有元素，为新元素的插入空间腾出位置
        // 源地址设置为p-nextdiff只是为了简化操作，之后p-nextdiff到p之间的内容会被覆盖
        // memmove不是移动是拷贝，不会破坏之前的数据
        memmove(p + reqlen, p - nextdiff, curlen - offset - 1 + nextdiff);

        // 将新节点的长度编码至后置节点
        // p+reqlen 定位到后置节点
        // reqlen 是新节点的长度
        zipPrevEncodeLength(p + reqlen, reqlen);

        // 更新到达表尾的偏移量，将新节点的长度也算上
        ZIPLIST_TAIL_OFFSET(zl) =
            intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + reqlen);

        // 如果新节点的后面有多于一个节点
        // 那么程序需要将 nextdiff 记录的字节数也计算到表尾偏移量中
        // 这样才能让表尾偏移量正确对齐表尾节点
        tail = zipEntry(p + reqlen);
        if (p[reqlen + tail.headersize + tail.len] != ZIP_END)
        {
            ZIPLIST_TAIL_OFFSET(zl) =
                intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + nextdiff);
        }
    }
    else
    {
        // 新元素是新的表尾节点
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p - zl);
    }

    // 当 nextdiff != 0 时，新节点的后继节点的（header 部分）长度已经被改变，
    // 所以需要级联地更新后续的节点
    if (nextdiff != 0)
    {
        offset = p - zl;
        zl = __ziplistCascadeUpdate(zl, p + reqlen);
        p = zl + offset;
    }

    // 一切搞定，将前置节点的长度写入新节点的 header
    p += zipPrevEncodeLength(p, prevlen);
    // 将节点值的长度写入新节点的 header
    p += zipEncodeLength(p, encoding, slen);
    // 写入节点值
    if (ZIP_IS_STR(encoding))
    {
        memcpy(p, s, slen);
    }
    else
    {
        zipSaveInteger(p, value, encoding);
    }

    // 更新列表的节点数量计数器
    ZIPLIST_INCR_LENGTH(zl, 1);

    return zl;
}

/* 将长度为 slen 的字符串 s 推入到 zl 中。
 * where 参数的值决定了推入的方向：
 * - 值为 ZIPLIST_HEAD 时，将新值推入到表头。
 * - 否则，将新值推入到表末端。
 * 函数的返回值为添加新值后的 ziplist 。
 */
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where)
{
    // 根据 where 参数的值，决定将值推入到表头还是表尾
    unsigned char *p;
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_END(zl);
    // 返回添加新值后的 ziplist
    return __ziplistInsert(zl, p, s, slen);
}

/* 根据给定索引，遍历列表，并返回索引指定节点的指针。
 * 如果索引为正，那么从表头向表尾遍历。
 * 如果索引为负，那么从表尾向表头遍历。
 * 正数索引从 0 开始，负数索引从 -1 开始。
 * 如果索引超过列表的节点数量，或者列表为空，那么返回 NULL 。
 */
unsigned char *ziplistIndex(unsigned char *zl, int index)
{
    unsigned char *p;
    zlentry entry;
    // 处理负数索引
    if (index < 0)
    {
        // 将索引转换为正数
        index = (-index) - 1;
        // 定位到表尾节点
        p = ZIPLIST_ENTRY_TAIL(zl);
        // 如果列表不为空，那么。。。
        if (p[0] != ZIP_END)
        {
            // 从表尾向表头遍历
            entry = zipEntry(p);
            while (entry.prevrawlen > 0 && index--)
            {
                // 前移指针
                p -= entry.prevrawlen;
                entry = zipEntry(p);
            }
        }
    }
    // 处理正数索引
    else
    {
        // 定位到表头节点
        p = ZIPLIST_ENTRY_HEAD(zl);
        // T = O(N)
        while (p[0] != ZIP_END && index--)
        {
            // 后移指针
            p += zipRawEntryLength(p);
        }
    }
    // 返回结果
    return (p[0] == ZIP_END || index > 0) ? NULL : p;
}

/*返回 p 所指向节点的后置节点。
 * 如果 p 为表末端，或者 p 已经是表尾节点，那么返回 NULL 。
 */
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p)
{
    // 操作一下，防止warning
    ((void)zl);
    // p 已经指向列表末端
    if (p[0] == ZIP_END)
    {
        return NULL;
    }
    // 指向后一节点
    p += zipRawEntryLength(p);
    if (p[0] == ZIP_END)
    {
        return NULL;
    }
    return p;
}

/* 返回 p 所指向节点的前置节点。
 * 如果 p 所指向为空列表，或者 p 已经指向表头节点，那么返回 NULL 。
 */
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p)
{
    zlentry entry;

    // 如果 p 指向列表末端（列表为空，或者刚开始从表尾向表头迭代）
    // 那么尝试取出列表尾端节点
    if (p[0] == ZIP_END)
    {
        p = ZIPLIST_ENTRY_TAIL(zl);
        // 尾端节点也指向列表末端，那么列表为空
        return (p[0] == ZIP_END) ? NULL : p;
    }
    // 如果 p 指向列表头，那么说明迭代已经完成
    else if (p == ZIPLIST_ENTRY_HEAD(zl))
    {
        return NULL;
    }
    // 既不是表头也不是表尾，从表尾向表头移动指针
    else
    {
        // 计算前一个节点的节点数
        entry = zipEntry(p);
        // 移动指针，指向前一个节点
        return p - entry.prevrawlen;
    }
}

/* 取出 p 所指向节点的值：
 * - 如果节点保存的是字符串，那么将字符串值指针保存到 *sstr 中，字符串长度保存到 *slen
 * - 如果节点保存的是整数，那么将整数保存到 *sval
 * 程序可以通过检查 *sstr 是否为 NULL 来检查值是字符串还是整数。
 * 提取值成功返回 1 ，
 * 如果 p 为空，或者 p 指向的是列表末端，那么返回 0 ，提取值失败。
 */
unsigned int ziplistGet(unsigned char *p, unsigned char **sstr, unsigned int *slen, long long *sval)
{

    zlentry entry;
    if (p == NULL || p[0] == ZIP_END)
        return 0;
    if (sstr)
        *sstr = NULL;

    // 取出 p 所指向的节点的各项信息，并保存到结构 entry 中
    entry = zipEntry(p);

    // 节点的值为字符串，将字符串长度保存到 *slen ，字符串保存到 *sstr
    if (ZIP_IS_STR(entry.encoding))
    {
        if (sstr)
        {
            *slen = entry.len;
            *sstr = p + entry.headersize;
        }
    }
    // 节点的值为整数，解码值，并将值保存到 *sval
    else
    {
        if (sval)
        {
            *sval = zipLoadInteger(p + entry.headersize, entry.encoding);
        }
    }
    return 1;
}

//如果 p 指向一个节点，那么新节点将放在原有节点的前面。
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen)
{
    return __ziplistInsert(zl, p, s, slen);
}

unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p)
{
    // 因为 __ziplistDelete 时会对 zl 进行内存重分配
    // 而内存充分配可能会改变 zl 的内存地址
    // 所以这里需要记录到达 *p 的偏移量
    // 这样在删除节点之后就可以通过偏移量来将 *p 还原到正确的位置
    size_t offset = *p - zl;
    zl = __ziplistDelete(zl, *p, 1);
    *p = zl + offset;
    return zl;
}

unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num)
{
    // 根据索引定位到节点
    unsigned char *p = ziplistIndex(zl, index);
    // 连续删除 num 个节点
    return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

unsigned int ziplistCompare(unsigned char *p, unsigned char *sstr, unsigned int slen)
{
    zlentry entry;
    unsigned char sencoding;
    long long zval, sval;
    if (p[0] == ZIP_END)
        return 0;
    // 取出节点
    entry = zipEntry(p);
    if (ZIP_IS_STR(entry.encoding))
    {
        // 节点值为字符串，进行字符串对比
        if (entry.len == slen)
        {
            return memcmp(p + entry.headersize, sstr, slen) == 0;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        // 节点值为整数，进行整数对比
        if (zipTryEncoding(sstr, slen, &sval, &sencoding))
        {
            zval = zipLoadInteger(p + entry.headersize, entry.encoding);
            return zval == sval;
        }
    }
    return 0;
}

/* 寻找节点值和 vstr 相等的列表节点，并返回该节点的指针。
 * 每两次对比之间跳过 skip 个节点。
 * 如果找不到相应的节点，则返回 NULL 。
 */
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip)
{
    int skipcnt = 0;
    unsigned char vencoding = 0;
    long long vll = 0;

    // 只要未到达列表末端，就一直迭代
    while (p[0] != ZIP_END)
    {
        unsigned int prevlensize, encoding, lensize, len;
        unsigned char *q;

        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
        // q指向数据
        q = p + prevlensize + lensize;

        if (skipcnt == 0)
        {
            // 对比字符串值
            if (ZIP_IS_STR(encoding))
            {
                if (len == vlen && memcmp(q, vstr, vlen) == 0)
                {
                    return p;
                }
            }
            else
            {
                // 因为传入值有可能被编码了，
                // 所以当第一次进行值对比时，程序会对传入值进行解码
                // 这个解码操作只会进行一次
                if (vencoding == 0)
                {
                    if (!zipTryEncoding(vstr, vlen, &vll, &vencoding))
                    {
                        // 如果解码失败，把vencoding设置成 UCHAR_MAX作为标记
                        vencoding = UCHAR_MAX;
                    }
                }
                // 如果vencoding不为UCHAR_MAX，说明有解析出整数
                // 对比整数值
                if (vencoding != UCHAR_MAX)
                {
                    long long ll = zipLoadInteger(q, encoding);
                    if (ll == vll)
                    {
                        return p;
                    }
                }
            }
            // 重置计数，保证下次对比是skip节点后
            skipcnt = skip;
        }
        else
        {
            skipcnt--;
        }
        // 后移指针，指向后置节点
        p = q + len;
    }
    return NULL;
}

unsigned int ziplistLen(unsigned char *zl)
{
    unsigned int len = 0;
    // 节点数小于 UINT16_MAX
    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX)
    {
        len = intrev16ifbe(ZIPLIST_LENGTH(zl));

        // 节点数大于 UINT16_MAX 时，需要遍历整个列表才能计算出节点数
    }
    else
    {
        unsigned char *p = zl + ZIPLIST_HEADER_SIZE;
        while (*p != ZIP_END)
        {
            p += zipRawEntryLength(p);
            len++;
        }
        // 如果统计出个数少于65535，会改变zllen
        if (len < UINT16_MAX)
            ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
    }

    return len;
}

size_t ziplistBlobLen(unsigned char *zl)
{
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}

void ziplistRepr(unsigned char *zl)
{
    unsigned char *p;
    int index = 0;
    zlentry entry;

    printf(
        "{total bytes %d} "
        "{length %u}\n"
        "{tail offset %u}\n",
        intrev32ifbe(ZIPLIST_BYTES(zl)),
        intrev16ifbe(ZIPLIST_LENGTH(zl)),
        intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)));
    p = ZIPLIST_ENTRY_HEAD(zl);
    while (*p != ZIP_END)
    {
        entry = zipEntry(p);
        printf(
            "{"
            "addr 0x%08lx, "
            "index %2d, "
            "offset %5ld, "
            "rl: %5u, "
            "hs %2u, "
            "pl: %5u, "
            "pls: %2u, "
            "payload %5u"
            "} ",
            (long unsigned)p,
            index,
            (unsigned long)(p - zl),
            entry.headersize + entry.len,
            entry.headersize,
            entry.prevrawlen,
            entry.prevrawlensize,
            entry.len);
        p += entry.headersize;
        if (ZIP_IS_STR(entry.encoding))
        {
            if (entry.len > 40)
            {
                fwrite(p, 40, 1, stdout);
                printf("...");
            }
            else
            {
                fwrite(p, entry.len, 1, stdout) == 0;
            }
        }
        else
        {
            printf("%lld", (long long)zipLoadInteger(p, entry.encoding));
        }
        printf("\n");
        p += entry.len;
        index++;
    }
    printf("{end}\n\n");
}