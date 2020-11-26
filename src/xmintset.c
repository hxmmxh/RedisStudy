//intset中所有成员的字节顺序固定为小端法，如果主机是大端法，每次读数写数时需要转换

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xmintset.h"
#include "xmmalloc.h"
#include "xmendianconv.h"


// 返回适用于传入值 v 的编码方式
static uint8_t _intsetValueEncoding(int64_t v)
{
    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
}

//根据给定的编码方式 enc ，返回集合的底层数组在 pos 索引上的元素。
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc)
{
    int64_t v64;
    int32_t v32;
    int16_t v16;
    // ((ENCODING*)is->contents) 首先将数组转换回被编码的类型
    // 然后 ((ENCODING*)is->contents)+pos 计算出元素在数组中的正确位置
    // 之后 member(&vEnc, ..., sizeof(vEnc)) 再从数组中拷贝出正确数量的字节
    // 如果有需要的话， memrevEncifbe(&vEnc) 会对拷贝出的字节进行大小端转换
    // 最后将值返回
    if (enc == INTSET_ENC_INT64)
    {
        memcpy(&v64, ((int64_t *)is->contents) + pos, sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    }
    else if (enc == INTSET_ENC_INT32)
    {
        memcpy(&v32, ((int32_t *)is->contents) + pos, sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    }
    else
    {
        memcpy(&v16, ((int16_t *)is->contents) + pos, sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

//根据集合的编码方式，返回底层数组在 pos 索引上的值
static int64_t _intsetGet(intset *is, int pos)
{
    return _intsetGetEncoded(is, pos, intrev32ifbe(is->encoding));
}

//根据集合的编码方式，将底层数组在 pos 位置上的值设为 value，这里默认编码可以容纳value
static void _intsetSet(intset *is, int pos, int64_t value)
{

    // 取出集合的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);
    // 根据编码 ((Enc_t*)is->contents) 将数组转换回正确的类型
    // 然后 ((Enc_t*)is->contents)[pos] 定位到数组索引上
    // 接着 ((Enc_t*)is->contents)[pos] = value 将值赋给数组
    // 最后， ((Enc_t*)is->contents)+pos 定位到刚刚设置的新值上
    // 如果有需要的话， memrevEncifbe 将对值进行大小端转换
    if (encoding == INTSET_ENC_INT64)
    {
        ((int64_t *)is->contents)[pos] = value;
        memrev64ifbe(((int64_t *)is->contents) + pos);
    }
    else if (encoding == INTSET_ENC_INT32)
    {
        ((int32_t *)is->contents)[pos] = value;
        memrev32ifbe(((int32_t *)is->contents) + pos);
    }
    else
    {
        ((int16_t *)is->contents)[pos] = value;
        memrev16ifbe(((int16_t *)is->contents) + pos);
    }
}

intset *intsetNew(void)
{
    intset *is = xm_malloc(sizeof(intset));
    // 初始的编码方式是int16
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;
}

//调整整数集合的内存空间大小，使其能容纳len个元素
//如果调整后的大小要比集合原来的大小要大，那么集合中原有元素的值不会被改变。
//返回值：调整大小后的整数集合
static intset *intsetResize(intset *is, uint32_t len)
{
    // 计算数组的空间大小
    uint32_t size = len * intrev32ifbe(is->encoding);
    // 根据空间大小，重新分配空间
    // 注意这里使用的是 zrealloc ，
    // 所以如果新空间大小比原来的空间大小要大，
    // 那么数组原有的数据会被保留
    is = xm_realloc(is, sizeof(intset) + size);
    return is;
}

// 在集合 is 的底层数组中查找值 value 所在的索引。
// 成功找到 value 时，函数返回 1 ，并将 *pos 的值设为 value 所在的索引。
// 没有找到时返回0，并将 *pos 的值设为 value 可以插入到数组中的位置。如果后面有元素需要后移
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos)
{
    //采用二分法查找，先初始化min,max和mid
    int min = 0, max = intrev32ifbe(is->length) - 1, mid = -1;
    int64_t cur = -1;
    //如果intset为空
    if (intrev32ifbe(is->length) == 0)
    {
        if (pos)
            *pos = 0;
        return 0;
    }
    else
    {
        // 因为底层数组是有序的，如果 value 比数组中最后一个值都要大
        // 那么 value 肯定不存在于集合中，
        // 并且应该将 value 添加到底层数组的最末端
        if (value > _intsetGet(is, intrev32ifbe(is->length) - 1))
        {
            if (pos)
                *pos = intrev32ifbe(is->length);
            return 0;
        }
        // 因为底层数组是有序的，如果 value 比数组中最前一个值都要小
        // 那么 value 肯定不存在于集合中，
        // 并且应该将它添加到底层数组的最前端
        else if (value < _intsetGet(is, 0))
        {
            if (pos)
                *pos = 0;
            return 0;
        }
    }
    //开始进行二分查找
    while (max >= min)
    {
        mid = (min + max) / 2;
        cur = _intsetGet(is, mid);
        if (value > cur)
        {
            min = mid + 1;
        }
        else if (value < cur)
        {
            max = mid - 1;
        }
        // 找到了
        else
        {
            break;
        }
    }
    // 检查是否已经找到了 value
    if (value == cur)
    {
        if (pos)
            *pos = mid;
        return 1;
    }
    // 如果每找到返回min的值
    else
    {
        if (pos)
            *pos = min;
        return 0;
    }
}

/*
已经确定了需要升级才会调用这个函数
根据值 value 所使用的编码方式，对整数集合的编码进行升级，并将值 value 添加到升级后的整数集合中。
返回值添加新元素之后的整数集合
*/
static intset *intsetUpgradeAndAdd(intset *is, int64_t value)
{
    // 当前的编码方式
    uint8_t curenc = intrev32ifbe(is->encoding);
    // 新值所需的编码方式
    uint8_t newenc = _intsetValueEncoding(value);
    // 当前集合的元素数量
    int length = intrev32ifbe(is->length);
    // 根据 value 的值，决定是将它添加到底层数组的最前端还是最后端
    // 注意，因为 value 的编码比集合原有的其他元素的编码都要大
    // 所以 value 要么大于集合中的所有元素，要么小于集合中的所有元素
    // 因此，value 只能添加到底层数组的最前端或最后端
    // 如果value是负的，现在的元素都要往后移一位
    int prepend = value < 0 ? 1 : 0;
    // 更新集合的编码方式
    is->encoding = intrev32ifbe(newenc);
    // 根据新编码对集合（的底层数组）进行空间调整
    is = intsetResize(is, intrev32ifbe(is->length) + 1);
    // 根据集合原来的编码方式，从底层数组中取出集合元素
    // 然后再将元素以新编码的方式添加到集合中
    // 从后往前移动
    // 取数时用当前的编码，插入时用新的编码
    while (length--)
        _intsetSet(is, length + prepend, _intsetGetEncoded(is, length, curenc));
    // 设置新值，根据 prepend 的值来决定是添加到数组头还是数组尾
    if (prepend)
        _intsetSet(is, 0, value);
    else
        _intsetSet(is, intrev32ifbe(is->length), value);
    // 更新整数集合的元素数量
    is->length = intrev32ifbe(intrev32ifbe(is->length) + 1);
    return is;
}

// 向前或向后移动指定索引范围内的数组元素
// 把从from一直到末尾的元素移动到以to开头的地方
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to)
{
    void *src, *dst;
    // 要移动的元素个数
    uint32_t bytes = intrev32ifbe(is->length) - from;
    // 集合的编码方式
    uint32_t encoding = intrev32ifbe(is->encoding);

    // 根据不同的编码 记录移动开始和结束的位置
    if (encoding == INTSET_ENC_INT64)
    {
        src = (int64_t *)is->contents + from;
        dst = (int64_t *)is->contents + to;
        bytes *= sizeof(int64_t);
    }
    else if (encoding == INTSET_ENC_INT32)
    {
        src = (int32_t *)is->contents + from;
        dst = (int32_t *)is->contents + to;
        bytes *= sizeof(int32_t);
    }
    else
    {
        src = (int16_t *)is->contents + from;
        dst = (int16_t *)is->contents + to;
        bytes *= sizeof(int16_t);
    }

    // 当内存发生局部重叠的时候，memmove保证拷贝的结果是正确的，memcpy不保证拷贝的结果的正确
    memmove(dst, src, bytes);
}

//success 的值指示添加是否成功：
//如果添加成功，那么将 *success 的值设为 1 。
//因为元素已存在而造成添加失败时，将 *success 的值设为 0
intset *intsetAdd(intset *is, int64_t value, uint8_t *success)
{
    // 计算编码 value 所需的长度
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    // 默认设置插入为成功
    if (success)
        *success = 1;
    // 如果需要升级
    if (valenc > intrev32ifbe(is->encoding))
    {
        return intsetUpgradeAndAdd(is, value);
    }
    // 不需要升级，先查找是否已经在集合中
    // 在的话，添加失败，返回
    if (intsetSearch(is, value, &pos))
    {
        if (success)
            *success = 0;
        return is;
    }
    // 不在的话需要插入进去
    else
    {
        // 首先扩展空间
        is = intsetResize(is, intrev32ifbe(is->length) + 1);
        // 如果不是插入到队尾，移动现有元素
        if (pos < intrev32ifbe(is->length))
            intsetMoveTail(is, pos, pos + 1);
        // 将新值设置到底层数组的指定位置中
        _intsetSet(is, pos, value);
        // 更新长度
        is->length = intrev32ifbe(intrev32ifbe(is->length) + 1);
        return is;
    }
}

/* success 的值指示删除是否成功：
 *  因值不存在而造成删除失败时该值为 0 。
 *  删除成功时该值为 1*/
intset *intsetRemove(intset *is, int64_t value, int *success)
{
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    // 默认设置标识值为删除失败
    if (success)
        *success = 0;
    // 只有当value的编码小于或等于集合的当前编码方式，并且搜索到了时，需要删除
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, &pos))
    {
        // 取出集合当前的元素数量
        uint32_t len = intrev32ifbe(is->length);
        // 设置标识值为删除成功
        if (success)
            *success = 1;
        // 如果 value 不是位于数组的末尾，那么需要对原本位于 value 之后的元素进行移动
        // 往前移动一格
        if (pos < (len - 1))
            intsetMoveTail(is, pos + 1, pos);
        // 缩小数组的大小，移除被删除元素占用的空间
        is = intsetResize(is, len - 1);
        // 更新集合的元素数量
        is->length = intrev32ifbe(len - 1);
    }
    return is;
}

//存在返回1，不存在返回-1
uint8_t intsetFind(intset *is, int64_t value)
{
    uint8_t valenc = _intsetValueEncoding(value);
    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, NULL);
}

int64_t intsetRandom(intset *is)
{
    //  rand() % intrev32ifbe(is->length) 根据元素数量计算一个随机索引
    return _intsetGet(is, rand() % intrev32ifbe(is->length));
}

//如果 pos 没超出数组的索引范围，那么返回 1 ，如果超出索引，那么返回 0
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value)
{
    if (pos < intrev32ifbe(is->length))
    {
        *value = _intsetGet(is, pos);
        return 1;
    }
    return 0;
}

uint32_t intsetLen(intset *is)
{
    return intrev32ifbe(is->length);
}

//包括整数集合的结构大小，以及整数集合所有元素的总大小
size_t intsetBlobLen(intset *is)
{
    return sizeof(intset) + intrev32ifbe(is->length) * intrev32ifbe(is->encoding);
}