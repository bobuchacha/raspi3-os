#include "app/__DLL_NAME__.h"
#include "stdio.h"

static long __DLL_NAME___double_value(long value)
{
    return value * 2;
}

static void __DLL_NAME___print_message(const char *name)
{
    char buffer[128];

    snprintf(buffer, sizeof(buffer), "[%s][__DLL_NAME__.dll]: hello from shared library\n", name);
    user_kernel_write(buffer);
}

__attribute__((section(".entrypoint")))
const __DLL_NAME__Api *
_shared_library_entry(void)
{
    static const __DLL_NAME__Api api = {
        __DLL_NAME_UPPER___ABI_VERSION,
        __DLL_NAME___double_value,
        __DLL_NAME___print_message,
    };

    return &api;
}