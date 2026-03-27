#ifndef ROS_USER___DLL_NAME_UPPER___H
#define ROS_USER___DLL_NAME_UPPER___H

#include "app/kernel.h"

#define __DLL_NAME_UPPER___ABI_VERSION 1UL
#define __DLL_NAME_UPPER___PATH "/lib/__DLL_NAME__.dll"

typedef struct __DLL_NAME__Api
{
    unsigned long abi_version;
    long (*double_value)(long value);
    void (*print_message)(const char *name);
} __DLL_NAME__Api;

typedef const __DLL_NAME__Api *(*__DLL_NAME__Entry)(void);

static inline const __DLL_NAME__Api *__DLL_NAME___open(void)
{
    __DLL_NAME__Entry entry = (__DLL_NAME__Entry)user_kernel_open_shared_library(__DLL_NAME_UPPER___PATH);
    if (!entry)
    {
        return 0;
    }

    return entry();
}

#endif