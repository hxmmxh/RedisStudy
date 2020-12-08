#include "xmrdb.h"

#include <arpa/inet.h>

#include "xmsds.h"
#include "xmt_string.h"

#include "lzf.h"

// 将长度为 len 的字符数组 p 写入到 rdb 中。
// 写入成功返回 len ，失败返回 -1
static int rdbWriteRaw(rio *rdb, void *p, size_t len)
{
    if (rdb && rioWrite(rdb, p, len) == 0)
        return -1;
    return len;
}

int rdbSaveType(rio *rdb, unsigned char type)
{
    return rdbWriteRaw(rdb, &type, 1);
}

int rdbLoadType(rio *rdb)
{
    unsigned char type;
    if (rioRead(rdb, &type, 1) == 0)
        return -1;
    return type;
}

time_t rdbLoadTime(rio *rdb)
{
    int32_t t32;
    if (rioRead(rdb, &t32, 4) == 0)
        return -1;
    return (time_t)t32;
}

int rdbSaveMillisecondTime(rio *rdb, long long t)
{
    int64_t t64 = (int64_t)t;
    return rdbWriteRaw(rdb, &t64, 8);
}

long long rdbLoadMillisecondTime(rio *rdb)
{
    int64_t t64;
    if (rioRead(rdb, &t64, 8) == 0)
        return -1;
    return (long long)t64;
}

int rdbSaveLen(rio *rdb, uint32_t len)
{
    unsigned char buf[2];
    size_t nwritten;
    // 6位整数，1字节
    if (len < (1 << 6))
    {
        buf[0] = (len & 0xFF) | (REDIS_RDB_6BITLEN << 6);
        if (rdbWriteRaw(rdb, buf, 1) == -1)
            return -1;
        nwritten = 1;
    }
    // 14位整数，2字节
    else if (len < (1 << 14))
    {
        buf[0] = ((len >> 8) & 0xFF) | (REDIS_RDB_14BITLEN << 6);
        buf[1] = len & 0xFF;
        if (rdbWriteRaw(rdb, buf, 2) == -1)
            return -1;
        nwritten = 2;
    }
    // 32位整数，5字节
    else
    {
        buf[0] = (REDIS_RDB_32BITLEN << 6);
        if (rdbWriteRaw(rdb, buf, 1) == -1)
            return -1;
        // 猜测是因为rdb文件要在网络上共享，所以以网络字节序保存
        // 将一个32位数从主机字节顺序转换成网络字节顺序，#include <arpa/inet.h>
        len = htonl(len);
        if (rdbWriteRaw(rdb, &len, 4) == -1)
            return -1;
        nwritten = 1 + 4;
    }

    return nwritten;
}

uint32_t rdbLoadLen(rio *rdb, int *isencoded)
{
    unsigned char buf[2];
    uint32_t len;
    int type;

    if (isencoded)
        *isencoded = 0;

    // 读入 length ，这个值可能已经被编码，也可能没有
    if (rioRead(rdb, buf, 1) == 0)
        return REDIS_RDB_LENERR;

    // 只取前两位
    type = (buf[0] & 0xC0) >> 6;

    // 特殊编码值，进行解码，取后6位
    if (type == REDIS_RDB_ENCVAL)
    {
        if (isencoded)
            *isencoded = 1;
        return buf[0] & 0x3F;
    }
    // 6 位整数
    else if (type == REDIS_RDB_6BITLEN)
    {
        return buf[0] & 0x3F;
    }
    // 14 位整数
    else if (type == REDIS_RDB_14BITLEN)
    {
        if (rioRead(rdb, buf + 1, 1) == 0)
            return REDIS_RDB_LENERR;
        return ((buf[0] & 0x3F) << 8) | buf[1];
    }
    // 32 位整数
    else
    {
        if (rioRead(rdb, &len, 4) == 0)
            return REDIS_RDB_LENERR;
        // 网络字节顺序转换成主机字节顺序
        return ntohl(len);
    }
}

/* 这个函数用在保存REDIS_ENCODING_INT编码的字符串对象上
 * 尝试使用特殊的整数编码来保存 value ，这要求它的值必须在给定范围之内。
 * 如果可以编码的话，将编码后的值保存在 enc 指针中，并返回值在编码后所需的长度。
 * 如果不能编码的话，返回 0 。
 * 多字节的整数是用小端法表示的
 */
int rdbEncodeInteger(long long value, unsigned char *enc)
{
    // 可用8位保存的整数
    if (value >= -(1 << 7) && value <= (1 << 7) - 1)
    {
        enc[0] = (REDIS_RDB_ENCVAL << 6) | REDIS_RDB_ENC_INT8;
        enc[1] = value & 0xFF;
        return 2;
    }
    // 可用16位保存的整数
    else if (value >= -(1 << 15) && value <= (1 << 15) - 1)
    {
        enc[0] = (REDIS_RDB_ENCVAL << 6) | REDIS_RDB_ENC_INT16;
        enc[1] = value & 0xFF;
        enc[2] = (value >> 8) & 0xFF;
        return 3;
    }
    // 可用32位保存的整数
    else if (value >= -((long long)1 << 31) && value <= ((long long)1 << 31) - 1)
    {
        enc[0] = (REDIS_RDB_ENCVAL << 6) | REDIS_RDB_ENC_INT32;
        enc[1] = value & 0xFF;
        enc[2] = (value >> 8) & 0xFF;
        enc[3] = (value >> 16) & 0xFF;
        enc[4] = (value >> 24) & 0xFF;
        return 5;
    }
    else
    {
        return 0;
    }
}

// 载入被编码成指定类型的编码整数对象。
// 如果 encoded 参数被设置了的话，那么可能会返回一个整数编码的字符串对象
// 没设置的话，返回为raw编码的字符串。
robj *rdbLoadIntegerObject(rio *rdb, int enctype, int encode)
{
    unsigned char enc[4];
    long long val;

    // 整数编码
    if (enctype == REDIS_RDB_ENC_INT8)
    {
        if (rioRead(rdb, enc, 1) == 0)
            return NULL;
        val = (signed char)enc[0];
    }
    // 保存的格式是小端法，取出数时别反了
    else if (enctype == REDIS_RDB_ENC_INT16)
    {
        uint16_t v;
        if (rioRead(rdb, enc, 2) == 0)
            return NULL;
        v = enc[0] | (enc[1] << 8);
        val = (int16_t)v;
    }
    else if (enctype == REDIS_RDB_ENC_INT32)
    {
        uint32_t v;
        if (rioRead(rdb, enc, 4) == 0)
            return NULL;
        v = enc[0] | (enc[1] << 8) | (enc[2] << 16) | (enc[3] << 24);
        val = (int32_t)v;
    }
    else
    {
        val = 0;
        printf("Unknown RDB integer encoding type");
    }

    if (encode)
        // 整数编码的字符串
        return createStringObjectFromLongLong(val);
    else
        // 未编码
        return createObject(REDIS_STRING, sdsfromlonglong(val));
}

// 尝试取出字符串中的整数，将编码后的值保存在enc中
int rdbTryIntegerEncoding(char *s, size_t len, unsigned char *enc)
{
    long long value;
    char *endptr, buf[32];

    // 尝试将值转换为整数
    // 解释str指向的字节字符串中的整数值,10表示十进制
    // endptr指向最后一个被解释成整数的字符的后一个字符
    // 如果s字符串只保存了一个整数且转换成功，endptr会指向\0
    value = strtoll(s, &endptr, 10);
    // 不能编码返回0
    if (endptr[0] != '\0')
        return 0;

    // 尝试将转换后的整数转换回字符串
    ll2string(buf, 32, value);

    // 检查两次转换后的整数值能否还原回原来的字符串
    // 如果不行的话，那么转换失败
    if (strlen(buf) != len || memcmp(buf, s, len))
        return 0;

    // 转换成功，对转换所得的整数进行特殊编码
    return rdbEncodeInteger(value, enc);
}

// 尝试对输入字符串 s 进行压缩，如果压缩成功，那么将压缩后的字符串保存到 rdb 中
// 函数在成功时返回保存压缩后的 s 所需的字节数
// 压缩失败或者内存不足时返回 0 
// 写入失败时返回 -1
int rdbSaveLzfStringObject(rio *rdb, unsigned char *s, size_t len)
{
    size_t comprlen, outlen;
    unsigned char byte;
    int n, nwritten = 0;
    void *out;

    
    // 压缩字符串
    // 字符串的长度必须大于4才会去压缩
    if (len <= 4)
        return 0;
    outlen = len - 4;
    if ((out = xm_malloc(outlen + 1)) == NULL)
        return 0;
    comprlen = lzf_compress(s, len, out, outlen);
    if (comprlen == 0)
    {
        zfree(out);
        return 0;
    }

    /* Data compressed! Let's save it on disk 
     *
     * 保存压缩后的字符串到 rdb 。
     */

    // 写入类型，说明这是一个 LZF 压缩字符串
    byte = (REDIS_RDB_ENCVAL << 6) | REDIS_RDB_ENC_LZF;
    if ((n = rdbWriteRaw(rdb, &byte, 1)) == -1)
        goto writeerr;
    nwritten += n;

    // 写入字符串压缩后的长度
    if ((n = rdbSaveLen(rdb, comprlen)) == -1)
        goto writeerr;
    nwritten += n;

    // 写入字符串未压缩时的长度
    if ((n = rdbSaveLen(rdb, len)) == -1)
        goto writeerr;
    nwritten += n;

    // 写入压缩后的字符串
    if ((n = rdbWriteRaw(rdb, out, comprlen)) == -1)
        goto writeerr;
    nwritten += n;

    zfree(out);

    return nwritten;

writeerr:
    zfree(out);
    return -1;
}