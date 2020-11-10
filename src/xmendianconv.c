#include "xmendianconv.h"

//把小端法表示的16位整数转化成大端法表示，0xAB到0xBA,这里一个字母代表一个字节
void memrev16(void *p)
{
    //x等同于p，t用于辅助交换
    unsigned char *x = p, t;
    t = x[0];
    x[0] = x[1];
    x[1] = t;
}

//把小端法表示的32位整数转化成大端法表示，0xABCD到0xDCBA
void memrev32(void *p)
{
    unsigned char *x = p, t;
    t = x[0];
    x[0] = x[3];
    x[3] = t;
    t = x[1];
    x[1] = x[2];
    x[2] = t;
}

////把小端法表示的32位整数转化成大端法表示，0xABCDEFGH到0xHGFEDCBA
void memrev64(void *p)
{
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[7];
    x[7] = t;
    t = x[1];
    x[1] = x[6];
    x[6] = t;
    t = x[2];
    x[2] = x[5];
    x[5] = t;
    t = x[3];
    x[3] = x[4];
    x[4] = t;
}

uint16_t intrev16(uint16_t v)
{
    memrev16(&v);
    return v;
}

uint32_t intrev32(uint32_t v)
{
    memrev32(&v);
    return v;
}

uint64_t intrev64(uint64_t v)
{
    memrev64(&v);
    return v;
}
