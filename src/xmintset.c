//intset中所有成员的字节顺序固定为小端法，如果主机是大端法，每次读数写数时需要转换

#include "xmintset.h"
#include "xmmalloc.h"
#include "xmendianconv.h"

// intset 的三种编码方式,应该分别为2，4，8
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

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
// 并将 *pos 的值设为 value 可以插入到数组中的位置。
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
    else
    {
        if (pos)
            *pos = min;
        return 0;
    }
}

































//success 的值指示添加是否成功：
//如果添加成功，那么将 *success 的值设为 1 。
//因为元素已存在而造成添加失败时，将 *success 的值设为 0