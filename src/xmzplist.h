#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1

#include "stdlib.h"

// 创建并返回一个新的 ziplist
unsigned char *ziplistNew(void);
// 将长度为 slen 的字符串 s 推入到 zl 中。
unsigned char *ziplistPush(unsigned char *zl, unsigned char *s, unsigned int slen, int where);
// 根据给定索引，遍历列表，并返回索引指定节点的指针。
unsigned char *ziplistIndex(unsigned char *zl, int index);
// 返回 p 所指向节点的后置节点。
unsigned char *ziplistNext(unsigned char *zl, unsigned char *p);
// 返回 p 所指向节点的前置节点。
unsigned char *ziplistPrev(unsigned char *zl, unsigned char *p);
// 取出 p 所指向节点的值
unsigned int ziplistGet(unsigned char *p, unsigned char **sval, unsigned int *slen, long long *lval);
// 将包含给定值 s 的新节点插入到给定的位置 p 中。
unsigned char *ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);
// 从 zl 中删除 *p 所指向的节点，并且原地更新 *p 所指向的位置，使得可以在迭代列表的过程中对节点进行删除
unsigned char *ziplistDelete(unsigned char *zl, unsigned char **p);
// 从 index 索引指定的节点开始，连续地从 zl 中删除 num 个节点。
unsigned char *ziplistDeleteRange(unsigned char *zl, unsigned int index, unsigned int num);
// 将 p 所指向的节点的值和 s 进行对比。 如果节点值和 sstr 的值相等，返回 1 ，不相等则返回 0 。
unsigned int ziplistCompare(unsigned char *p, unsigned char *s, unsigned int slen);
// 寻找节点值和 vstr 相等的列表节点，并返回该节点的指针。每两次对比之间跳过 skip 个节点。
unsigned char *ziplistFind(unsigned char *p, unsigned char *vstr, unsigned int vlen, unsigned int skip);
// 返回 ziplist 中的节点个数
unsigned int ziplistLen(unsigned char *zl);
// 返回整个 ziplist 占用的内存字节数
size_t ziplistBlobLen(unsigned char *zl);
// 打印ziplist的一些基本参数
void ziplistRepr(unsigned char *zl);