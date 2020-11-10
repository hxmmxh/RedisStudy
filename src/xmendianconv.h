#ifndef HXM_ENDIANCONV_H
#define HXM_ENDIANCONV_H

#include <stdint.h>

//做简化处理，直接定义字节序为小端
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#define BYTE_ORDER LITTLE_ENDIAN

//字节在小端和大端之间相互转换
void memrev16(void *p);
void memrev32(void *p);
void memrev64(void *p);

//例如0x1234在小端法表示的值为v，获得它在大端法下的值，可以相互转换
uint16_t intrev16(uint16_t v);
uint32_t intrev32(uint32_t v);
uint64_t intrev64(uint64_t v);


//主要用于intset结构中，intset中字节顺序固定为小端法，如果主机是大端法，每次读数写数时需要转换
#if (BYTE_ORDER == LITTLE_ENDIAN)
#define memrev16ifbe(p)
#define memrev32ifbe(p)
#define memrev64ifbe(p)
#define intrev16ifbe(v) (v)
#define intrev32ifbe(v) (v)
#define intrev64ifbe(v) (v)
#else
#define memrev16ifbe(p) memrev16(p)
#define memrev32ifbe(p) memrev32(p)
#define memrev64ifbe(p) memrev64(p)
#define intrev16ifbe(v) intrev16(v)
#define intrev32ifbe(v) intrev32(v)
#define intrev64ifbe(v) intrev64(v)
#endif


//网络字节序是大端法，只有在小端时需要转化， host to network unsigned
#if (BYTE_ORDER == BIG_ENDIAN)
#define htonu64(v) (v)
#define ntohu64(v) (v)
#else
#define htonu64(v) intrev64(v)
#define ntohu64(v) intrev64(v)
#endif


#endif