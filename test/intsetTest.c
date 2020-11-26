#include "test.h"
#include "xmintset.h"
#include "xmendianconv.h"

#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

void intsetRepr(intset *is)
{
    int i;
    for (i = 0; i < intrev32ifbe(is->length); i++)
    {
        uint64_t n;
        intsetGet(is, i, &n);
        printf("%lld\n", n);
    }
    printf("\n");
}
void error(char *err)
{
    printf("%s\n", err);
    exit(1);
}

void ok(void)
{
    printf("OK\n");
}

long long usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000000) + tv.tv_usec;
}

// 如果e为真，什么都不干，不然调用_assert
#define assert(_e) ((_e) ? (void)0 : (_assert(#_e, __FILE__, __LINE__), exit(1)))
void _assert(char *estr, char *file, int line)
{
    printf("\n\n=== ASSERTION FAILED ===\n");
    printf("==> %s:%d '%s' is not true\n", file, line, estr);
}

intset *createSet(int bits, int size)
{
    uint64_t mask = (1 << bits) - 1;
    uint64_t i, value;
    intset *is = intsetNew();

    for (i = 0; i < size; i++)
    {
        if (bits > 32)
        {
            value = (rand() * rand()) & mask;
        }
        else
        {
            value = rand() & mask;
        }
        is = intsetAdd(is, value, NULL);
    }
    return is;
}

//证明有序
void checkConsistency(intset *is)
{
    int i;

    for (i = 0; i < (intrev32ifbe(is->length) - 1); i++)
    {
        uint32_t encoding = intrev32ifbe(is->encoding);

        if (encoding == INTSET_ENC_INT16)
        {
            int16_t *i16 = (int16_t *)is->contents;
            assert(i16[i] < i16[i + 1]);
        }
        else if (encoding == INTSET_ENC_INT32)
        {
            int32_t *i32 = (int32_t *)is->contents;
            assert(i32[i] < i32[i + 1]);
        }
        else
        {
            int64_t *i64 = (int64_t *)is->contents;
            assert(i64[i] < i64[i + 1]);
        }
    }
}

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

int main()
{
    uint8_t success;
    int i;
    intset *is;

    srand((unsigned int)time(NULL));
    printf("Value encodings: ");
    {
        assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(-9223372036854775808ull) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+9223372036854775807ull) == INTSET_ENC_INT64);
        ok();
    }
    printf("Basic adding: ");
    {
        is = intsetNew();
        is = intsetAdd(is, 5, &success);
        assert(success);
        is = intsetAdd(is, 6, &success);
        assert(success);
        is = intsetAdd(is, 4, &success);
        assert(success);
        is = intsetAdd(is, 4, &success);
        assert(!success);
        ok();
    }
    printf("Large number of random adds: ");
    {
        int inserts = 0;
        is = intsetNew();
        for (i = 0; i < 1024; i++)
        {
            is = intsetAdd(is, rand() % 0x800, &success);
            if (success)
                inserts++;
        }
        assert(intrev32ifbe(is->length) == inserts);
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int32: ");
    {
        is = intsetNew();
        is = intsetAdd(is, 32, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is, 65535, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is, 32));
        assert(intsetFind(is, 65535));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is, 32, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is, -65535, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is, 32));
        assert(intsetFind(is, -65535));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int64: ");
    {
        is = intsetNew();
        is = intsetAdd(is, 32, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is, 4294967295, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is, 32));
        assert(intsetFind(is, 4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is, 32, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is, -4294967295, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is, 32));
        assert(intsetFind(is, -4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int32 to int64: ");
    {
        is = intsetNew();
        is = intsetAdd(is, 65535, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is, 4294967295, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is, 65535));
        assert(intsetFind(is, 4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is, 65535, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is, -4294967295, NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is, 65535));
        assert(intsetFind(is, -4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Stress lookups: ");
    {
        long num = 100000, size = 10000;
        int i, bits = 20;
        long long start;
        is = createSet(bits, size);
        checkConsistency(is);

        start = usec();
        for (i = 0; i < num; i++)
            intsetFind(is, rand() % ((1 << bits) - 1));
        printf("%ld lookups, %ld element set, %lldusec\n", num, size, usec() - start);
    }

    printf("Stress add+delete: ");
    {
        int i, v1, v2;
        is = intsetNew();
        for (i = 0; i < 0xffff; i++)
        {
            v1 = rand() % 0xfff;
            is = intsetAdd(is, v1, NULL);
            assert(intsetFind(is, v1));

            v2 = rand() % 0xfff;
            is = intsetRemove(is, v2, NULL);
            assert(!intsetFind(is, v2));
        }
        checkConsistency(is);
        ok();
    }
}