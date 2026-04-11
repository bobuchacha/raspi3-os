#include <stdio.h>

#include "user_runtime.h"

SYS_EXPORT(Init);
SYS_EXPORT(Deinit);
SYS_EXPORT(__SYS_NAME___dispatch);
SYS_EXPORT(DriverLoop);

static unsigned long g_dispatch_count;

int Init(void* base) {
    (void)base;
    g_dispatch_count = 0;
    printf("__SYS_NAME__: C driver online\r\n");
    return 0;
}

int Deinit(void* base) {
    (void)base;
    printf("__SYS_NAME__: C driver offline\r\n");
    return 0;
}

unsigned long __SYS_NAME___dispatch(unsigned long op, unsigned long value) {
    g_dispatch_count++;
    return op + value + g_dispatch_count;
}

int DriverLoop(void* base) {
    (void)base;
    for (;;) {
        sleepMs(1000);
    }
}