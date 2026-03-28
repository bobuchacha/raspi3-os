#include "ros_user_runtime.h"
#include "sample_abi.h"

static unsigned long g_dispatch_count;
static unsigned long g_last_value;

int Init(void* base) {
    (void)base;
    g_dispatch_count = 0;
    g_last_value = 0;
    ros_write_line("sample_driver: user-mode driver online");
    return 0;
}

int Deinit(void* base) {
    (void)base;
    ros_write_line("sample_driver: user-mode driver offline");
    return 0;
}

unsigned long sample_driver_dispatch(unsigned long op, unsigned long value) {
    g_dispatch_count++;

    if (op == SAMPLE_DRIVER_OP_STATUS) {
        return 4096UL + g_dispatch_count;
    }
    if (op == SAMPLE_DRIVER_OP_PULSE) {
        g_last_value = value;
        return (value ^ 0x5A5AUL) + g_dispatch_count;
    }

    return 0;
}

unsigned long sample_driver_last_value(void) {
    return g_last_value;
}
