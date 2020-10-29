#include "xmmalloc.h"

#include <stdlib.h>

void *xm_malloc(size_t size)
{
    void *ptr = malloc(size);
    return ptr;
}

void *xm_calloc(size_t size)
{
    void *ptr = calloc(1, size);
    return ptr;
}

void *xm_realloc(void *ptr, size_t size)
{
    void *newptr = realloc(ptr, size);
    return newptr;
}

void xm_free(void *ptr)
{
    if (ptr == NULL)
        return;
    free(ptr);
}