#ifndef HXM_ADLIST_H
#define HXM_ADLIST_H

//节点
typedef struct listNode
{
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

//链表
typedef struct list
{
    listNode *head;
    listNode *tail;
    unsigned long len; //链表所包含的节点数量

    void *(*dup)(void *ptr);              //复制链表节点所保存的值
    void *(*free)(void *ptr);             //释放链表节点所保存的值
    void *(*match)(void *ptr, void *key); //用于对比链表节点所保存的值和另一个输入值是否相等
} list;

//链表迭代器
typedef struct listIter
{
    listNode *next; // 当前迭代到的节点
    int direction;  // 迭代的方向
} listIter;

#define listSetDupMethod(l, m) ((l)->dup = (m))
#define listSetFreeMethod(l, m) ((l)->free = (m))
#define listSetMatchMethod(l, m) ((l)->match = (m))

#define listGetDupMethod(l) ((l)->dup)
#define listGetFree(l) ((l)->free)
#define listGetMatchMethod(l) ((l)->match)

#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)

#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)

list *listCreate(void);
void listRelease(list *list);

list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
// after为0插到之前，为1插到之后
list *listInsertNode(list *list, listNode *old_node, void *value, int after);
void listDelNode(list *list, listNode *node);

//复制整个链表
list *listDup(list *orig);
//取出链表的表尾节点，并将它移动到表头，成为新的表头节点
void listRotate(list *list);
//查找链表 list 中值和 key 匹配的节点。 如果匹配成功，那么第一个匹配的节点会被返回。如果没有匹配任何节点，那么返回 NULL 
listNode *listSearchKey(list *list, void *key);
//返回链表在给定索引上的值。 索引以 0 为起始，也可以是负数，-1 表示链表最后一个节点，如果索引超出范围（out of range），返回 NULL
listNode *listIndex(list *list, long index);


// 从表头向表尾进行迭代
#define AL_START_HEAD 0
// 从表尾到表头进行迭代
#define AL_START_TAIL 1
listIter *listGetIterator(list *list, int direction);
//返回迭代器当前所指向的节点。并且把迭代器往下移。删除当前节点是允许的，但不能修改链表里的其他节点
listNode *listNext(listIter *iter);
//释放迭代器
void listReleaseIterator(listIter *iter);
//将迭代器的方向设置为 AL_START_HEAD, 并将迭代指针重新指向表头节点。
void listRewind(list *list, listIter *li);
//将迭代器的方向设置为 AL_START_TAIL, 并将迭代指针重新指向表尾节点。
void listRewindTail(list *list, listIter *li);




#endif