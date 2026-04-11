#include <stdio.h>

#include "app/__LIB_NAME__.h"

DLL_EXPORT(Init);
DLL_EXPORT(Deinit);
DLL_EXPORT(__LIB_NAME___sum);

static unsigned long g_call_count;

int Init(void* base) {
    (void)base;
    g_call_count = 0;
    printf("__LIB_NAME__: C library ready\r\n");
    return 0;
}

int Deinit(void* base) {
    (void)base;
    printf("__LIB_NAME__: C library stopping\r\n");
    return 0;
}

long __LIB_NAME___sum(long left, long right) {
    g_call_count++;
    return left + right + (long)g_call_count;
}