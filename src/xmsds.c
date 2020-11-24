#include "xmsds.h"
#include "xmmalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>


sds sdsnewlen(const void *init, size_t initlen)
{
    sdshdr *sh;
    if (init)
    {
        // +1是为了结尾的'\0'字符,下面会填充内存，所以这里不需要初始化为0
        sh = xm_malloc(sizeof(sdshdr) + initlen + 1);
    }
    else
    {
        // calloc 将分配的内存全部初始化为 0
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
    return sdsnewlen(NULL, 0);
}

sds sdsdup(const sds s)
{
    return sdsnewlen(s, sdslen(s));
}

void sdsfree(sds s)
{
    if (s == NULL)
        return;
    //把指针地址往前移，因为要释放整个sds结构
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
    // s目前的空余空间已经足够，无须再进行扩展，直接返回
    if (free >= addlen)
        return s;
    size_t len = sdslen(s);
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    // s 最少需要的长度
    size_t newlen = len + addlen;
    // 根据新长度，为 s 分配新空间所需的大小
    if (newlen < SDS_MAX_PREALLOC)
        // 如果新长度小于 SDS_MAX_PREALLOC,那么为它分配两倍于所需长度的空间
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

//使buf中存放的字符串的长度为len+incr,并将 \0 放到新字符串的尾端
//这个函数是在调用 sdsMakeRoomFor() 对字符串进行扩展，然后用户在字符串尾部写入了某些内容之后，用来正确更新 free 和 len 属性的。
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
    // 如果 len 比字符串的现有长度小，那么直接返回，不做动作
    if (len <= curlen)
        return s;

    s = sdsMakeRoomFor(s, len - curlen);
    if (s == NULL)
        return NULL;

    //将新分配的空间用 0 填充，结尾的'\0'也在这里加上
    memset(s + curlen, 0, (len - curlen + 1));

    sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    sh->free = sh->free - (len - sh->len);
    sh->len = len;

    return s;
}

// 将长度为 len 的字符串 t 追加到 sds 的字符串末尾
static sds sdscatlen(sds s, const void *t, size_t len)
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

// 将字符串 t 的前 len 个字符复制到 sds s 当中，并在字符串的最后添加终结符。
static sds sdscpylen(sds s, const char *t, size_t len)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    size_t totlen = sh->free + sh->len;
    // 如果 s 的 buf 长度不满足 len ，那么扩展它
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
    // 如果有需要，前移字符串内容
    if (sh->buf != sp)
        memmove(sh->buf, sp, len);
    sh->buf[len] = '\0';
    sh->free = sh->free + (sh->len - len);
    sh->len = len;
    return s;
}

// 索引从 0 开始，最大为 sdslen(s) - 1
// 索引可以是负数， sdslen(s) - 1 == -1
void sdsrange(sds s, int start, int end)
{
    sdshdr *sh = (sdshdr *)(void *)(s - sizeof(sdshdr));
    size_t newlen, len = sdslen(s);
    if (len == 0)
        return;
    // 如果索引是负数，调整为正数
    if (start < 0)
    {
        start = len + start;
        if (start < 0)
            start = 0;
    }
    if (end < 0)
    {
        end = len + end;
        if (end < 0)
            end = 0;
    }
    newlen = (start > end) ? 0 : (end - start) + 1;
    if (newlen != 0)
    {
        if (start >= (signed)len)
        {
            newlen = 0;
        }
        else if (end >= (signed)len)
        {
            end = len - 1;
            newlen = (start > end) ? 0 : (end - start) + 1;
        }
    }
    else
    {
        start = 0;
    }
    if (start && newlen)
        memmove(sh->buf, sh->buf + start, newlen);
    sh->buf[newlen] = 0;
    sh->free = sh->free + (sh->len - newlen);
    sh->len = newlen;
}

void sdstolower(sds s)
{
    int len = sdslen(s), j;
    for (j = 0; j < len; j++)
        s[j] = tolower(s[j]);
}

void sdstoupper(sds s)
{
    int len = sdslen(s), j;
    for (j = 0; j < len; j++)
        s[j] = toupper(s[j]);
}

int sdscmp(const sds s1, const sds s2)
{
    size_t l1, l2, minlen;
    int cmp;
    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1, s2, minlen);
    if (cmp == 0)
        return l1 - l2;
    return cmp;
}

//long long的最大值：9223372036854775807（>10^18）
//long long的最小值：-9223372036854775808
//最大19位，加上正负号，加上结尾的\0
#define SDS_LLSTR_SIZE 21

//把long long 转化成字符串，存放在s中，返回s的长度
static int sdsll2str(char *s, long long value)
{
    unsigned long long v;
    char *p;
    size_t l;
    //下面得到的是倒转的
    v = (value < 0) ? -value : value;
    p = s;
    do
    {
        *p++ = '0' + (v % 10); //等同于*p=  , p++
        v /= 10;
    } while (v);
    if (value < 0)
        *p++ = '-';
    //计算长度
    l = p - s;
    *p = '\0';
    //倒转回来，\0不用倒转，所以p--
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

// 把unsigned long long 转化成字符串，存放在s中，返回s的长度
static int sdsull2str(char *s, unsigned long long v)
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

static sds sdscatvprintf(sds s, const char *fmt, va_list ap)
{
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    // ？
    size_t buflen = strlen(fmt) * 2;

    if (buflen > sizeof(staticbuf))
    {
        buf = xm_malloc(buflen);
        if (buf == NULL)
            return NULL;
    }
    else
    {
        buflen = sizeof(staticbuf);
    }

    while (1)
    {
        //vsnprintf会在数组的尾端自动加上一个\0字节，所以这里把数组的倒数第二个字节设为'\0'？？
        buf[buflen - 2] = '\0';
        va_copy(cpy, ap);
        vsnprintf(buf, buflen, fmt, cpy);
        // 每次分配失败，都会把buf的空间增加到两倍
        if (buf[buflen - 2] != '\0')
        {
            if (buf != staticbuf)
                xm_free(buf);
            buflen *= 2;
            buf = xm_malloc(buflen);
            if (buf == NULL)
                return NULL;
            continue;
        }
        break;
    }

    t = sdscat(s, buf);
    if (buf != staticbuf)
        xm_free(buf);
    return t;
}

sds sdscatprintf(sds s, const char *fmt, ...)
{
    va_list ap;
    char *t;
    va_start(ap, fmt);
    t = sdscatvprintf(s, fmt, ap);
    va_end(ap);
    return t;
}

/*
 * %s - C String C字符串
 * %S - SDS string sds字符串
 * %i - signed int 有符号整数
 * %I - 64 bit signed integer (long long, int64_t) 64位的整数
 * %u - unsigned int 无符号整数
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t) 64位的无符号整数
 * %% - Verbatim "%" character. %字符
*/

sds sdscatfmt(sds s, char const *fmt, ...)
{
    sdshdr *sh = (void *)(s - (sizeof(struct sdshdr)));
    size_t initlen = sdslen(s);
    const char *f = fmt;
    int i;
    va_list ap;

    va_start(ap, fmt);
    f = fmt;     //指向下一个要处理的fmt中的字符
    i = initlen; //要写入的sds中的插入位置
    while (*f)
    {
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        // 保证sds中还能再追加一个字符
        if (sh->free == 0)
        {
            s = sdsMakeRoomFor(s, 1);
            sh = (void *)(s - (sizeof(struct sdshdr)));
        }

        switch (*f)
        {
        //如果是%,说明后面跟着的是控制字符
        case '%':
            next = *(f + 1);
            f++;
            switch (next)
            {
                // 打印字符串
            case 's':
            case 'S':
                str = va_arg(ap, char *);
                l = (next == 's') ? strlen(str) : sdslen(str);
                if (sh->free < l)
                {
                    s = sdsMakeRoomFor(s, l);
                    sh = (void *)(s - (sizeof(struct sdshdr)));
                }
                memcpy(s + i, str, l);
                sh->len += l;
                sh->free -= l;
                i += l;
                break;
            //打印整数
            case 'i':
            case 'I':
                if (next == 'i')
                    num = va_arg(ap, int);
                else
                    num = va_arg(ap, long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    // 把数字转化成字符串
                    l = sdsll2str(buf, num);
                    if (sh->free < l)
                    {
                        s = sdsMakeRoomFor(s, l);
                        sh = (void *)(s - (sizeof(struct sdshdr)));
                    }
                    memcpy(s + i, buf, l);
                    sh->len += l;
                    sh->free -= l;
                    i += l;
                }
                break;
            // 打印无符号整数
            case 'u':
            case 'U':
                if (next == 'u')
                    unum = va_arg(ap, unsigned int);
                else
                    unum = va_arg(ap, unsigned long long);
                {
                    char buf[SDS_LLSTR_SIZE];
                    l = sdsull2str(buf, unum);
                    if (sh->free < l)
                    {
                        s = sdsMakeRoomFor(s, l);
                        sh = (void *)(s - (sizeof(struct sdshdr)));
                    }
                    memcpy(s + i, buf, l);
                    sh->len += l;
                    sh->free -= l;
                    i += l;
                }
                break;
            // 其他值
            default:
                s[i++] = next;
                sh->len += 1;
                sh->free -= 1;
                break;
            }
            break;
        default:
            s[i++] = *f;
            sh->len += 1;
            sh->free -= 1;
            break;
        }
        f++;
    }
    va_end(ap);
    // 在结尾加上\0
    s[i] = '\0';
    return s;
}

sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count)
{
    //先预估要分配5个sds节点，防止频繁扩展空间
    int elements = 0, slots = 5, start = 0, j;
    sds *tokens;

    if (seplen < 1 || len < 0)
        return NULL;

    tokens = xm_malloc(sizeof(sds) * slots);
    if (tokens == NULL)
        return NULL;

    if (len == 0)
    {
        *count = 0;
        return tokens;
    }
    // 并不需要查找到最后一个字符，如果距离末尾的长度小于分隔符的长度，就已经可以停下了
    for (j = 0; j < (len - (seplen - 1)); j++)
    {
        // 保证节点还可以容纳下一个节点以及结尾的节点
        if (slots < elements + 2)
        {
            sds *newtokens;
            // 空间预分配，多分配一些空间
            slots *= 2;
            newtokens = xm_realloc(tokens, sizeof(sds) * slots);
            // 分配错误后记得清除之前已经分配的空间
            if (newtokens == NULL)
                goto cleanup;
            tokens = newtokens;
        }
        // 查找分隔符
        if ((seplen == 1 && *(s + j) == sep[0]) || (memcmp(s + j, sep, seplen) == 0))
        {
            // start表示上一个分隔符后的第一个字符
            tokens[elements] = sdsnewlen(s + start, j - start);
            if (tokens[elements] == NULL)
                goto cleanup;
            elements++;
            start = j + seplen;
            // 因为for()里还有个j++，所以这里j = j+seplen-1
            j = j + seplen - 1;
        }
    }
    // 加入最后一个元素
    tokens[elements] = sdsnewlen(s + start, len - start);
    if (tokens[elements] == NULL)
        goto cleanup;
    elements++;
    *count = elements;
    return tokens;

cleanup:
{
    int i;
    for (i = 0; i < elements; i++)
        sdsfree(tokens[i]);
    xm_free(tokens);
    *count = 0;
    return NULL;
}
}

void sdsfreesplitres(sds *tokens, int count)
{
    if (!tokens)
        return;
    while (count--)
        sdsfree(tokens[count]);
    xm_free(tokens);
}

sds sdscatrepr(sds s, const char *p, size_t len)
{

    s = sdscatlen(s, "\"", 1);

    while (len--)
    {
        switch (*p)
        {
        case '\\':
        case '"':
            s = sdscatprintf(s, "\\%c", *p);
            break;
        case '\n':
            s = sdscatlen(s, "\\n", 2);
            break;
        case '\r':
            s = sdscatlen(s, "\\r", 2);
            break;
        case '\t':
            s = sdscatlen(s, "\\t", 2);
            break;
        case '\a':
            s = sdscatlen(s, "\\a", 2);
            break;
        case '\b':
            s = sdscatlen(s, "\\b", 2);
            break;
        default:
            if (isprint(*p))
                s = sdscatprintf(s, "%c", *p);
            else
                //0表示添加前导0进行填充，2表示最小宽度
                s = sdscatprintf(s, "\\x%02x", (unsigned char)*p);
            break;
        }
        p++;
    }

    return sdscatlen(s, "\"", 1);
}

sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen)
{
    size_t j, i, l = sdslen(s);
    // 遍历输入字符串
    for (j = 0; j < l; j++)
    {
        // 遍历映射
        for (i = 0; i < setlen; i++)
        {
            // 替换字符串
            if (s[j] == from[i])
            {
                s[j] = to[i];
                break;
            }
        }
    }
    return s;
}

sds sdsjoin(char **argv, int argc, char *sep)
{
    sds join = sdsempty();
    int j;
    for (j = 0; j < argc; j++)
    {
        join = sdscat(join, argv[j]);
        if (j != argc - 1)
            join = sdscat(join, sep);
    }
    return join;
}

