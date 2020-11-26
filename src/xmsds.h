
#ifndef HXM_SDS_H
#define HXM_SDS_H

#include <sys/types.h>
#include <stdarg.h>

/*
buf的结构
len '\0' free
|-----------|'\0'|-------------|
buf为空时
'\0'+free
buf满时
len+'\0'
*/

typedef struct sdshdr
{
    int len;    //记录buf数组中已使用字节的数量
    int free;   //记录buf数组中未使用字节的数量
    char buf[]; //用于保存字符串的字节数组
} sdshdr;

//在使用SDS时sds也就是buf的地址
//减去shshdr的长度也就得到了shshdr结构的地址
typedef char *sds;

//最大预分配长度,在空间预分配时用的上
#define SDS_MAX_PREALLOC (1024 * 1024)

// 下面是常用的API函数

// 声明和定义中都要加static
static size_t sdslen(const sds s);
static size_t sdsavail(const sds s);

// 创建并返回一个sds
sds sdsnew(const char *init);
// 根据给定的初始化字符串 init 和字符串长度 initlen创建一个新的 sds
sds sdsnewlen(const void *init, size_t initlen);
// 创建一个不包含内容的SDS,但是包含'\0'
sds sdsempty(void);
// 创建一个给定sds的副本
sds sdsdup(const sds s);

// 释放给定的 SDS
void sdsfree(sds s);
// 清空字符串，但不释放空间
void sdsclear(sds s);

// 将sds扩充至指定长度，新增的内容以 0 字节填充
sds sdsgrowzero(sds s, size_t len);
sds sdsMakeRoomFor(sds s, size_t addlen);
// 回收空闲空间，即free会变回0，并会释放空间
sds sdsRemoveFreeSpace(sds s);
// 返回给定 sds 分配的内存字节数,整个shshdr占用的空间
size_t sdsAllocSize(sds s);
// 使sds的长度增加incr，如果 incr 参数为负数，那么对字符串进行右截断操作。
void sdsIncrLen(sds s, int incr);

// 将给定字符串 t 追加到 sds 的末尾
sds sdscat(sds s, const char *t);
// 将另一个 sds 追加到一个 sds 的末尾
sds sdscatsds(sds s, const sds t);
// 将字符串复制到 sds 当中，覆盖原有的字符
sds sdscpy(sds s, const char *t);
// 将长度为 len 的字符串 p 以带引号（quoted）的格式，追加到给定 sds 的末尾
// 不可打印的字符用"\n\r\a...."或者十六进制"\x<hex-number>"表示
sds sdscatrepr(sds s, const char *p, size_t len);

// 将 sds 字符串中的所有字符转换为小写
void sdstolower(sds s);
// 将 sds 字符串中的所有字符转换为大写
void sdstoupper(sds s);
// 对比两个 sds,相等返回 0 ，s1 较大返回正数， s2 较大返回负数
int sdscmp(const sds s1, const sds s2);
// 将字符串 s 中所有在 from 中出现的字符，替换成 to 中的字符
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
// 将C字符串数组中的所有字符串加入sds中，并且用sep分隔每个字符串
sds sdsjoin(char **argv, int argc, char *sep);

//根据输入的 long long 值 value ，创建一个 SDS，在创建long类型的字符串对象时用的上
sds sdsfromlonglong(long long value);

// 保留 SDS 给定区间内的数据， 不在区间内的数据会被覆盖或清除，给出的是闭区间
void sdsrange(sds s, int start, int end);
// 对 sds 左右两端进行修剪，清除其中 cset 指定的所有字符
sds sdstrim(sds s, const char *cset);

// 打印任意数量个字符串，并将这些字符串追加到给定 sds 的末尾
sds sdscatprintf(sds s, const char *fmt, ...);
// sdscatprintf的简易版和快速版
sds sdscatfmt(sds s, char const *fmt, ...);

// 使用分隔符 sep 对 s 进行分割，返回一个 sds 字符串的数组.*count 会被设置为返回数组元素的数量。
// 分隔符可以的是包含多个字符的字符串
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
// 释放 tokens 数组中 count 个 sds
void sdsfreesplitres(sds *tokens, int count);

// 在头文件中用inline时务必加入static，保证一定是内联的
// 最简单的两个函数被声明为inline
// 头文件中用inline函数时要加入static
// static函数仅在本文件内可见
// inline 必须与函数定义体放在一起才能使函数成为内联，
// 仅将inline 放在函数声明前面不起任何作用,可以不用在声明处加上

static inline size_t sdslen(const sds s)
{
    sdshdr *sh = (sdshdr *)(void *)(s - (sizeof(sdshdr)));
    return sh->len;
}

static inline size_t sdsavail(const sds s)
{
    sdshdr *sh = (sdshdr *)(void *)(s - (sizeof(sdshdr)));
    return sh->free;
}

//定义和声明在util.c中
//int stringmatchlen(const char *p, int plen, const char *s, int slen, int nocase);
//int stringmatch(const char *p, const char *s, int nocase);

// 把以k m G等为单位的表示空间大小的值转化成整数，*err为1表示解析失败，为0表示成功
long long memtoll(const char *p, int *err);
// 如果len的长度不够会发生截断，返回最终的长度
int ll2string(char *s, size_t len, long long value);
// 返回1表示成功，0表示失败
int string2ll(const char *s, size_t slen, long long *value);
int string2l(const char *s, size_t slen, long *value);

//int d2string(char *buf, size_t len, double value);
//sds getAbsolutePath(char *filename);
//int pathIsBaseName(char *path);










#endif

