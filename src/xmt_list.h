#include "xmobject.h"
#include "xmadlist.h"
#include "xmzplist.h"
#include "xmsds.h"
#include "xmt_string.h"

#include "xmserver.h"
#include "xmclient.h"


//列表迭代器对象
typedef struct
{
    // 列表对象
    robj *subject;
    // 对象所使用的编码
    unsigned char encoding;
    // 迭代的方向
    unsigned char direction;
    // 压缩列表索引，迭代压缩列表编码的列表时使用
    unsigned char *zi;
    // 链表节点的指针，迭代双端链表编码的列表时使用
    listNode *ln;
} listTypeIterator;

// 迭代列表时使用的记录结构，用于保存迭代器，以及迭代器返回的列表节点。
typedef struct
{
    // 列表迭代器
    listTypeIterator *li;
    // 压缩列表节点索引
    unsigned char *zi;
    // 双端链表节点指针
    listNode *ln;
} listTypeEntry;


// 创建一个 LINKEDLIST 编码的列表对象
robj *createListObject(void);
// 创建一个 ZIPLIST 编码的列表对象
robj *createZiplistObject(void);
// 释放列表对象
void freeListObject(robj *o);

// 对输入值 value 进行检查，看是否需要将 subject 从 ziplist 转换为双端链表，以便保存值 value 
void listTypeTryConversion(robj *subject, robj *value);
// 将给定元素添加到列表的表头或表尾。参数 where 决定了新元素添加的位置
void listTypePush(robj *subject, robj *value, int where);
// 从列表的表头或表尾中弹出一个元素。取出并且删除这个元素
robj *listTypePop(robj *subject, int where);
// 返回列表的节点数量
unsigned long listTypeLength(robj *subject);

// 创建并返回一个列表迭代器。参数 index 决定开始迭代的列表索引， 参数 direction 则决定了迭代的方向
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction);
// 释放迭代器
void listTypeReleaseIterator(listTypeIterator *li);
// 使用 entry 结构记录迭代器当前指向的节点，并将迭代器的指针移动到下一个元素。
// 如果列表中还有元素可迭代，那么返回 1 ，否则，返回 0 。
int listTypeNext(listTypeIterator *li, listTypeEntry *entry);
// 返回 entry 结构当前所保存的列表节点。如果 entry 没有记录任何节点，那么返回 NULL 。
robj *listTypeGet(listTypeEntry *entry);
// 将对象 value 插入到列表节点的之前或之后
void listTypeInsert(listTypeEntry *entry, robj *value, int where);
// 将当前节点的值和对象 o 进行对比， 函数在两值相等时返回 1 ，不相等时返回 0
int listTypeEqual(listTypeEntry *entry, robj *o);
// 删除 entry 所指向的节点
void listTypeDelete(listTypeEntry *entry);
//  将列表的底层编码从压缩列表转换成双端链表
void listTypeConvert(robj *subject, int enc);
