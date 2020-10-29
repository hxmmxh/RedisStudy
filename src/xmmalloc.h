//1.0.0版，简单封装系统Malloc函数，没有其他功能，2020-8-18



#ifndef HXM_MALLOC_H
#define HXM_MALLOC_H

#include <malloc.h>

//??每次分配空间时，都会在空间的开始处记录这个空间的大小，占用的空间为一个size_t的大小


void *xm_malloc(size_t size);//初始值不确定
void *xm_calloc(size_t size);//每一位会被初始化为0
void *xm_realloc(void *ptr, size_t size);
void xm_free(void *ptr);

#endif