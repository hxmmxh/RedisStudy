#include "xmsds.h"
#include "xmmalloc.h"

#include <stdlib.h>
#include <string.h>

/*
 * 根据给定的初始化字符串 init 和字符串长度 initlen
 * 创建一个新的 sds
 *
 * 参数
 *  init ：初始化字符串指针
 *  initlen ：初始化字符串的长度
 *
 * 返回值
 *  sds ：创建成功返回 sdshdr 相对应的 sds
 *        创建失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */

sds sdsnewlen(const void *init, size_t initlen)
{
    sdshdr *sh;
    if (init)
    {
        // zmalloc 不初始化所分配的内存
        // +1是为了结尾的'\0'字符
        sh = xm_malloc(sizeof(sdshdr) + initlen + 1);
    }
    else
    {
        // zcalloc 将分配的内存全部初始化为 0
        sh = xm_calloc(sizeof(sdshdr) + initlen + 1);
    }
    // 内存分配失败，返回
    if (sh == NULL)
        return NULL;

    // 设置初始化长度
    sh->len = initlen;
    // 新 sds 不预留任何空间
    sh->free = 0;
    // 如果有指定初始化内容，将它们复制到 sdshdr 的 buf 中
    // T = O(N)
    if (initlen && init)
        memcpy(sh->buf, init, initlen);
    // 以 \0 结尾
    sh->buf[initlen] = '\0';
    // 返回 buf 部分，而不是整个 sdshdr
    return (char *)sh->buf;
}

sds sdsnew(const char *init)
{
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

sds sdsempty(void)
{
    return sdsnewlen("", 0);
}

sds sdsdup(const sds s)
{
    return sdsnewlen(s, sdslen(s));
}

void sdsfree(sds s)
{
    if (s == NULL)
        return;
    //把指针地址往前移，要释放整个sds结构
    xm_free(s - sizeof(sdshdr));
}

void sdsclear(sds s)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    sh->free += sh->len;
    sh->len = 0;
    sh->buf[0] = '\0';
}

sds sdsMakeRoomFor(sds s, size_t addlen)
{
    size_t free = sdsavail(s);
    if (free >= addlen)
        return s;
    size_t len = sdslen(s);
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    size_t newlen = len + addlen;
    // 根据新长度，为 s 分配新空间所需的大小
    if (newlen < SDS_MAX_PREALLOC)
        // 如果新长度小于 SDS_MAX_PREALLOC
        // 那么为它分配两倍于所需长度的空间
        newlen *= 2;
    else
        // 否则，分配长度为目前长度加上 SDS_MAX_PREALLOC
        newlen += SDS_MAX_PREALLOC;
    //重新分配空间
    sdshdr *newsh = xm_realloc(sh, sizeof(sdshdr) + newlen + 1);
    // 内存不足，分配失败，返回
    if (newsh == NULL)
        return NULL;
    // 更新 sds 的空余长度
    newsh->free = newlen - len;
    // 返回 sds
    return newsh->buf;
}

sds sdsRemoveFreeSpace(sds s)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    // 进行内存重分配，让 buf 的长度仅仅足够保存字符串内容
    sh = xm_realloc(sh, sizeof(sdshdr) + sh->len + 1);
    // 把空余空间设为 0
    sh->free = 0;
    return sh->buf;
}

size_t sdsAllocSize(sds s)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    return sizeof(*sh) + sh->len + sh->free + 1;
}

//使buf中存放的字符串的长度为len+incr
void sdsIncrLen(sds s, int incr)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    assert(sh->free >= incr);
    sh->len += incr;
    sh->free -= incr;
    s[sh->len] = '\0';
}

sds sdsgrowzero(sds s, size_t len)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    size_t curlen = sh->len;
    if (len <= curlen)
        return s;

    s = sdsMakeRoomFor(s, len - curlen);
    if (s == NULL)
        return NULL;

    memset(s + curlen, 0, (len - curlen + 1)); //结尾的那个0也加上

    sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    sh->free = sh->free - (len - sh->len);
    sh->len = len;

    return s;
}

sds sdscatlen(sds s, const void *t, size_t len)
{
    s = sdsMakeRoomFor(s, len);
    if (s == NULL)
        return NULL;
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    size_t curlen = sh->len;
    memcpy(s + curlen, t, len);
    sh->len = curlen + len;
    sh->free = sh->free - len;
    s[curlen + len] = '\0';
    return s;
}

sds sdscat(sds s, const char *t)
{
    return sdscatlen(s, t, strlen(t));
}

sds sdscatsds(sds s, const sds t)
{
    return sdscatlen(s, t, sdslen(t));
}

sds sdscpylen(sds s, const char *t, size_t len)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    size_t totlen = sh->free + sh->len;
    if (totlen < len)
    {
        s = sdsMakeRoomFor(s, len - sh->len);
        if (s == NULL)
            return NULL;
        sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
        totlen = sh->free + sh->len;
    }
    memcpy(s, t, len);
    s[len] = '\0';
    sh->len = len;
    sh->free = totlen - len;
    return s;
}

sds sdscpy(sds s, const char *t)
{
    return sdscpylen(s, t, strlen(t));
}

//long long的最大值：9223372036854775807（>10^18）
//long long的最小值：-9223372036854775808
//最大19位，加上正负号，加上结尾的\0
#define SDS_LLSTR_SIZE 21

//把long long 转化成字符串，存放在s中，返回s的长度
int sdsll2str(char *s, long long value)
{
    unsigned long long v;
    char *p;
    size_t l;
    //下面得到的是倒转的
    v = (value < 0) ? -value : value;
    p = s;
    do
    {
        *p++ = '0' + (v % 10);
        v /= 10;
    } while (v);
    if (value < 0)
        *p++ = '-';
    l = p - s;
    *p = '\0';
    //倒转回来
    p--;
    char aux;
    while (s < p)
    {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

int sdsull2str(char *s, unsigned long long v)
{
    char *p, aux;
    size_t l;

    p = s;
    do
    {
        *p++ = '0' + (v % 10);
        v /= 10;
    } while (v);

    l = p - s;
    *p = '\0';

    p--;
    while (s < p)
    {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

sds sdsfromlonglong(long long value)
{
    char buf[SDS_LLSTR_SIZE];
    int len = sdsll2str(buf, value);
    return sdsnewlen(buf, len);
}

sds sdstrim(sds s, const char *cset)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    char *start, *end, *sp, *ep;
    size_t len;
    sp = start = s;
    ep = end = s + sdslen(s) - 1;
    //char *strchr(const char *str, int c)
    //在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置
    //未找到该字符则返回 NULL
    while (sp <= end && strchr(cset, *sp))
        sp++;
    while (ep > start && strchr(cset, *ep))
        ep--;
    len = (sp > ep) ? 0 : ((ep - sp) + 1);
    if (sh->buf != sp)
        memmove(sh->buf, sp, len);
    sh->buf[len] = '\0';
    sh->free = sh->free + (sh->len - len);
    sh->len = len;
    return s;
}