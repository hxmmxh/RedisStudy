#include <stdlib.h>
#include "xmadlist.h"
#include "xmmalloc.h"

//创建成功返回链表，失败返回 NULL
list *listCreate(void)
{
    list *list;
    if ((list = xm_malloc(sizeof(*list))) == NULL)
        return NULL;
    list->head = list->tail = NULL;
    list->len = 0;
    //不能list->dup = list->free = list->match = NULL，因为这三者类型不同
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

//会同时释放链表中所有节点
void listRelease(list *list)
{
    unsigned long len;
    listNode *current, *next;
    current = list->head;
    len = list->len;
    while (len--)
    {
        next = current->next;
        if (list->free)
            list->free(current->value); //释放节点中的数据
        xm_free(current);               //释放每个节点
        current = next;
    }
    xm_free(list); //释放链表结构
}

list *listAddNodeHead(list *list, void *value)
{
    listNode *node;
    if ((node = xm_malloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (list->len == 0)
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }
    else
    {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
    return list;
}

list *listAddNodeTail(list *list, void *value)
{
    listNode *node;
    if ((node = xm_malloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (list->len == 0)
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }
    else
    {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

list *listInsertNode(list *list, listNode *old_node, void *value, int after)
{
    listNode *node;
    if ((node = xm_malloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;

    if (after)
    {
        node->prev = old_node;
        node->next = old_node->next;
        if (list->tail == old_node)
        {
            list->tail = node;
        }
    }
    else
    {
        node->next = old_node;
        old_node->prev = node;
        if (list->head == old_node)
        {
            list->head = node;
        }
    }

    if (node->prev != NULL)
    {
        node->prev->next = node;
    }
    if (node->next != NULL)
    {
        node->next->prev = node;
    }
    list->len++;
    return list;
}

void listDelNode(list *list, listNode *node);
{
    if (node->prev != NULL)
        node->prev->next = node->next;
    if (node->next != NULL)
        node->next->prev = node->prev;
    if (node == list->head)
        list->head = node->next;
    else if (node == list->tail)
        list->tail = node->prev;
    if (list->free)
        list->free(node->value);
    xm_free(node);
    list->len--;
}

listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;
    if ((iter = xm_malloc(sizeof(*iter))) == NULL)
        return NULL;
    //迭代器的初始节点
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;

    iter->direction = direction;
    return iter;
}

listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;
    if (current != NULL)
    {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
}

void listReleaseIterator(listIter *iter)
{
    xm_free(iter);
}

void listRewind(list *list, listIter *li)
{
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

void listRewindTail(list *list, listIter *li)
{
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

list *listDup(list *orig)
{
    list *copy;
    if ((list = xm_malloc(sizeof(*list))) == NULL)
        return NULL;
    listIter *iter;
    listNode *node;

    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;

    iter = listGetIterator(orig, AL_START_HEAD);
    while ((node = listNext(iter)) != NULL)
    {
        void *value;
        //如果链表有设置值复制函数 dup ，那么对值的复制将使用复制函数进行，否则，新节点将和旧节点共享同一个指针
        if (copy->dup)
        {
            value = copy->dup(node->value);
            //如果复制失败，释放整个copy列表，并返回NULL
            if (value == NULL)
            {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        }
        else
        {
            value = node->value;
        }
        // 直接调用之前的函数将节点添加到链表尾部，同时也要判断是否成功
        if (listAddNodeTail(copy, value) == NULL)
        {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }
    // 别忘记释放迭代器
    listReleaseIterator(iter);
    return copy
}

void listRotate(list *list)
{
    listNode *tail = list->tail;
    if (tail == list->head)
        return;
    tail->prev->next = NULL;
    list->tail = tail->prev;
    tail->prev = NULL;
    tail->next = list->head;
    list->head->prev = tail;
    list->head = tail;
}

listNode *listSearchKey(list *list, void *key)
{
    listIter *iter;
    iter = listGetIterator(list, AL_START_HEAD);
    listNode *node;
    while ((node = listNext(iter)) != NULL)
    {
        //对比操作由链表的 match 函数负责进行，如果没有设置 match 函数，那么直接通过对比值的指针来决定是否匹配。
        if (list->match)
        {
            if (list->match(node->value, key))
            {
                // 别忘记释放迭代器
                listReleaseIterator(iter);
                return node;
            }
        }
        else
        {
            if (key == node->value)
            {
                listReleaseIterator(iter);
                return node;
            }
        }
    }
    // 别忘记释放迭代器
    listReleaseIterator(iter);
    return NULL;
}

listNode *listIndex(list *list, long index)
{
    listNode *node;
    /*
    listIter *iter;
    if (index > 0)
    {
        iter = listGetIterator(list, AL_START_HEAD);
        while ((node = listNext(iter)) != NULL && index-- >= 0)
            ;
        return node;
    }
    else
    {
        iter = listGetIterator(list, AL_START_TAIL);
        while ((node = listNext(iter)) != NULL && index++ <= 0)
            ;
        return node;
    }
    */
    if (index < 0)
    {
        index = (-index) - 1;
        node = list->tail;
        while (index-- && node)
            node = node->prev;
        // 如果索引为正数，从表头开始查找
    }
    else
    {
        node = list->head;
        while (index-- && node)
            node = node->next;
    }
    return node;
}