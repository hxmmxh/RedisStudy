
#ifndef HXM_SDS_H
#define HXM_SDS_H

#include <sys/types.h>
#include <stdarg.h>

typedef struct sdshdr
{
    int len;    //记录buf数组中已使用字节的数量
    int free;   //记录buf数组中未使用字节的数量
    char buf[]; //用于保存字符串的字节数组
} sdshdr;

//在使用SDS时sds也就是buf的地址
//减去shshdr的长度也就得到了shshdr结构的地址
typedef char *sds;

#define SDS_MAX_PREALLOC (1024 * 1024) //最大预分配长度

//下面是常用的API函数

//创建一个给定C字符串的SDS
sds sdsnewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);      //创建一个不包含内容的SDS
sds sdsdup(const sds s); //创建一个给定的副本

size_t sdslen(const sds s);   //返回已使用空间字节数
size_t sdsavail(const sds s); //返回未使用空间字节数

void sdsfree(sds s);

void sdsclear(sds s); //清空字符串，但不释放空间

sds sdsgrowzero(sds s, size_t len);//将sds扩充至指定长度，新增的内容以 0 字节填充

sds sdscatlen(sds s, const void *t, size_t len);//将长度为 len 的字符串 t 追加到 sds 的字符串末尾
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);

sds sdscpylen(sds s, const char *t, size_t len);//将字符串 t 的前 len 个字符复制到 sds s 当中，
sds sdscpy(sds s, const char *t);

sds sdsMakeRoomFor(sds s, size_t addlen); //进行扩展,确保buf能放下addlen长的字符串，即addlen + 1 长度的空余空间
sds sdsRemoveFreeSpace(sds s);            //回收空闲空间，即free会变回0，并会释放空间
size_t sdsAllocSize(sds s);               //返回给定 sds 分配的内存字节数,整个shshdr占用的空间
void sdsIncrLen(sds s, int incr);         //使sds的长度增加incr，如果 incr 参数为负数，那么对字符串进行右截断操作。

//把long long 转化成字符串
int sdsll2str(char *s, long long value);
//把unsigned long long 转化成字符串
int sdsull2str(char *s, unsigned long long v);
//根据输入的 long long 值 value ，创建一个 SDS
sds sdsfromlonglong(long long value);

// 对 sds 左右两端进行修剪，清除其中 cset 指定的所有字符
sds sdstrim(sds s, const char *cset)















//最简单的两个函数被声明为inline
//头文件中用inline函数时要加入static
//static函数仅在本文件内可见
//inline 必须与函数定义体放在一起才能使函数成为内联，
//仅将inline 放在函数声明前面不起任何作用,可以不用在声明处加上

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

#endif
