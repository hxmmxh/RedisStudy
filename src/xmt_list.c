#include "xmt_list.h"
#include "xmmalloc.h"

robj *createListObject(void)
{
    list *l = listCreate();
    robj *o = createObject(REDIS_LIST, l);
    // 设置节点值释放函数
    listSetFreeMethod(l, decrRefCountVoid);
    o->encoding = REDIS_ENCODING_LINKEDLIST;
    return o;
}

robj *createZiplistObject(void)
{
    unsigned char *zl = ziplistNew();
    robj *o = createObject(REDIS_LIST, zl);
    o->encoding = REDIS_ENCODING_ZIPLIST;
    return o;
}

void freeListObject(robj *o)
{
    switch (o->encoding)
    {
    case REDIS_ENCODING_LINKEDLIST:
        listRelease((list *)o->ptr);
        break;
    case REDIS_ENCODING_ZIPLIST:
        xm_free(o->ptr);
        break;
    default:
        // redisPanic("Unknown list encoding type");
    }
}

void listTypeTryConversion(robj *subject, robj *value)
{
    // 确保 subject 为 ZIPLIST 编码
    if (subject->encoding != REDIS_ENCODING_ZIPLIST)
        return;
    // 字符串过长
    if (sdsEncodedObject(value) && sdslen(value->ptr) > server.list_max_ziplist_value)
        // 将编码转换为双端链表
        listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);
}

void listTypePush(robj *subject, robj *value, int where)
{
    // 检查是否需要转换编码？
    listTypeTryConversion(subject, value);
    // 压缩列表的长度过长，也需要转换编码
    if (subject->encoding == REDIS_ENCODING_ZIPLIST &&
        ziplistLen(subject->ptr) >= server.list_max_ziplist_entries)
        listTypeConvert(subject, REDIS_ENCODING_LINKEDLIST);

    // 压缩列表的插入工作
    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
    {
        int pos = (where == REDIS_HEAD) ? ZIPLIST_HEAD : ZIPLIST_TAIL;
        // 解码，把整数编码的字符串对象转换成字符串编码
        // 因为ziplistPush不会识别long类型的内容
        value = getDecodedObject(value);
        subject->ptr = ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), pos);
        // 注意引用次数减一
        decrRefCount(value);
    }
    // 双端链表的插入工作
    else if (subject->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        if (where == REDIS_HEAD)
        {
            listAddNodeHead(subject->ptr, value);
        }
        else
        {
            listAddNodeTail(subject->ptr, value);
        }
        // 引用次数加一
        incrRefCount(value);
    }
    else
    {
        //redisPanic("Unknown list encoding");
    }
}

robj *listTypePop(robj *subject, int where)
{

    robj *value = NULL;

    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *p;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        // 决定弹出元素的位置
        int pos = (where == REDIS_HEAD) ? 0 : -1;

        p = ziplistIndex(subject->ptr, pos);
        // 将字符串值指针保存到 vstr 中，字符串长度保存到 vlen
        // 如果节点保存的是整数，那么将整数保存到 vlong
        if (ziplistGet(p, &vstr, &vlen, &vlong))
        {
            // 为被弹出元素创建对象
            if (vstr)
            {
                value = createStringObject((char *)vstr, vlen);
            }
            else
            {
                value = createStringObjectFromLongLong(vlong);
            }
            // 从 ziplist 中删除被弹出元素
            subject->ptr = ziplistDelete(subject->ptr, &p);
        }
    }
    // 双端链表
    else if (subject->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        list *list = subject->ptr;
        listNode *ln;
        if (where == REDIS_HEAD)
        {
            ln = listFirst(list);
        }
        else
        {
            ln = listLast(list);
        }
        // 删除被弹出节点
        if (ln != NULL)
        {
            value = listNodeValue(ln);
            incrRefCount(value);
            // 这里会调用设置好的free函数，让引用次数减一
            listDelNode(list, ln);
        }
    }
    else
    {
        //redisPanic("Unknown list encoding");
    }
    // 返回节点对象
    return value;
}

unsigned long listTypeLength(robj *subject)
{
    if (subject->encoding == REDIS_ENCODING_ZIPLIST)
    {
        return ziplistLen(subject->ptr);
    }
    else if (subject->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        return listLength((list *)subject->ptr);
    }
    else
    {
        // redisPanic("Unknown list encoding");
    }
}

listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction)
{

    listTypeIterator *li = xm_malloc(sizeof(listTypeIterator));
    li->subject = subject;
    li->encoding = subject->encoding;
    li->direction = direction;

    if (li->encoding == REDIS_ENCODING_ZIPLIST)
    {
        li->zi = ziplistIndex(subject->ptr, index);
    }
    else if (li->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        li->ln = listIndex(subject->ptr, index);
    }
    else
    {
        //redisPanic("Unknown list encoding");
    }
    return li;
}

void listTypeReleaseIterator(listTypeIterator *li)
{
    xm_free(li);
}

int listTypeNext(listTypeIterator *li, listTypeEntry *entry)
{
    entry->li = li;
    if (li->encoding == REDIS_ENCODING_ZIPLIST)
    {
        // 记录当前节点到 entry
        entry->zi = li->zi;
        // 移动迭代器的指针
        if (entry->zi != NULL)
        {
            if (li->direction == REDIS_TAIL)
                li->zi = ziplistNext(li->subject->ptr, li->zi);
            else
                li->zi = ziplistPrev(li->subject->ptr, li->zi);
            return 1;
        }
    }
    else if (li->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        // 记录当前节点到 entry
        entry->ln = li->ln;
        // 移动迭代器的指针
        if (entry->ln != NULL)
        {
            if (li->direction == REDIS_TAIL)
                li->ln = li->ln->next;
            else
                li->ln = li->ln->prev;
            return 1;
        }
    }
    else
    {
        // redisPanic("Unknown list encoding");
    }
    // 列表元素已经全部迭代完
    return 0;
}

robj *listTypeGet(listTypeEntry *entry)
{
    listTypeIterator *li = entry->li;
    robj *value = NULL;

    if (li->encoding == REDIS_ENCODING_ZIPLIST)
    {
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        if (ziplistGet(entry->zi, &vstr, &vlen, &vlong))
        {
            if (vstr)
            {
                value = createStringObject((char *)vstr, vlen);
            }
            else
            {
                value = createStringObjectFromLongLong(vlong);
            }
        }
    }
    else if (li->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        value = listNodeValue(entry->ln);
        incrRefCount(value);
    }
    else
    {
        //redisPanic("Unknown list encoding");
    }
    return value;
}

void listTypeInsert(listTypeEntry *entry, robj *value, int where)
{
    robj *subject = entry->li->subject;
    if (entry->li->encoding == REDIS_ENCODING_ZIPLIST)
    {
        // 返回对象未编码的值
        value = getDecodedObject(value);
        // 插到节点之后
        if (where == REDIS_TAIL)
        {
            unsigned char *next = ziplistNext(subject->ptr, entry->zi);
            if (next == NULL)
            {
                // next 是表尾节点，push 新节点到表尾
                subject->ptr = ziplistPush(subject->ptr, value->ptr, sdslen(value->ptr), REDIS_TAIL);
            }
            else
            {
                // 插入到到节点之后
                subject->ptr = ziplistInsert(subject->ptr, next, value->ptr, sdslen(value->ptr));
            }
        }
        // 插到节点之前
        else
        {
            subject->ptr = ziplistInsert(subject->ptr, entry->zi, value->ptr, sdslen(value->ptr));
        }
        decrRefCount(value);
    }
    else if (entry->li->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        if (where == REDIS_TAIL)
        {
            listInsertNode(subject->ptr, entry->ln, value, AL_START_TAIL);
        }
        else
        {
            listInsertNode(subject->ptr, entry->ln, value, AL_START_HEAD);
        }
        incrRefCount(value);
    }
    else
    {
        //redisPanic("Unknown list encoding");
    }
}

int listTypeEqual(listTypeEntry *entry, robj *o)
{
    listTypeIterator *li = entry->li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST)
    {
        return ziplistCompare(entry->zi, o->ptr, sdslen(o->ptr));
    }
    else if (li->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        return equalStringObjects(o, listNodeValue(entry->ln));
    }
    else
    {
        // redisPanic("Unknown list encoding");
    }
}

void listTypeDelete(listTypeEntry *entry)
{

    listTypeIterator *li = entry->li;

    if (li->encoding == REDIS_ENCODING_ZIPLIST)
    {

        unsigned char *p = entry->zi;

        li->subject->ptr = ziplistDelete(li->subject->ptr, &p);

        // 删除节点之后，更新迭代器的指针
        if (li->direction == REDIS_TAIL)
            li->zi = p;
        else
            li->zi = ziplistPrev(li->subject->ptr, p);
    }
    else if (entry->li->encoding == REDIS_ENCODING_LINKEDLIST)
    {
        // 记录后置节点
        listNode *next;
        if (li->direction == REDIS_TAIL)
            next = entry->ln->next;
        else
            next = entry->ln->prev;
        // 删除当前节点
        listDelNode(li->subject->ptr, entry->ln);
        // 删除节点之后，更新迭代器的指针
        li->ln = next;
    }
    else
    {
        //redisPanic("Unknown list encoding");
    }
}

void listTypeConvert(robj *subject, int enc)
{

    listTypeIterator *li;
    listTypeEntry entry;

    // 转换成双端链表
    if (enc == REDIS_ENCODING_LINKEDLIST)
    {
        // 创建一个新的双端链表
        list *l = listCreate();
        listSetFreeMethod(l, decrRefCountVoid);

        // 用迭代器遍历 ziplist ，并将里面的值全部添加到双端链表中
        li = listTypeInitIterator(subject, 0, REDIS_TAIL);
        while (listTypeNext(li, &entry))
            listAddNodeTail(l, listTypeGet(&entry));
        listTypeReleaseIterator(li);
        // 更新编码
        subject->encoding = REDIS_ENCODING_LINKEDLIST;

        // 释放原来的 ziplist
        xm_free(subject->ptr);

        // 更新对象值指针
        subject->ptr = l;
    }
}