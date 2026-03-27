#include "app/kernel.h"
#include "stdlib.h"
#include "string.h"

void *malloc(size_t size)
{
    return (void *)user_kernel_alloc((unsigned long)(size == 0 ? 1 : size));
}

void free(void *ptr)
{
    user_kernel_free(ptr);
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *ptr = malloc(total);

    if (!ptr)
    {
        return 0;
    }

    memset(ptr, 0, total);
    return ptr;
}

void abort(void)
{
    user_kernel_exit(1);
    while (1)
    {
    }
}

void exit(int code)
{
    user_kernel_exit((unsigned long)code);
    while (1)
    {
    }
}