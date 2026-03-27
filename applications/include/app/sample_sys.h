#ifndef ROS_APP_SAMPLE_SYS_H
#define ROS_APP_SAMPLE_SYS_H

#include "app/import.h"

#define SAMPLE_SYS_QUERY_ABI_VERSION 0UL
#define SAMPLE_SYS_QUERY_NEXT_HEARTBEAT_MS 1UL
#define SAMPLE_SYS_QUERY_TICKS_MS 2UL

static inline long sample_sys_query_status(unsigned long query) {
    return user_kernel_extension_invoke("sample_sys", "query_status", query, 0);
}
static inline long sample_sys_do_add(unsigned long a, unsigned long b) {
    return user_kernel_extension_invoke("sample_sys", "do_add", a, b);
}
static inline long sample_sys_get_loaded_address(void) {
    return user_kernel_extension_invoke("sample_sys", "get_loadaddr", 0, 0);
}
static inline long kprint(const char* fmt, ...) {
    return user_kernel_extension_invoke("sample_sys", "kprint", (unsigned long)fmt, 0);
}

#endif