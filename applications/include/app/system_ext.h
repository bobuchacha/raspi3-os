#ifndef ROS_APP_SYSTEM_EXT_H
#define ROS_APP_SYSTEM_EXT_H

#include "string.h"
#include "stdarg.h"
#include "stdio.h"

#define SYSTEM_EXT_API_VERSION 1U
#define SYSTEM_EXT_API_VA 0xBFF000UL

typedef struct {
    unsigned long total_bytes;
    unsigned long free_bytes;
    unsigned long page_size;
    unsigned long free_pages;
    unsigned long heap_total_bytes;
    unsigned long heap_used_bytes;
    unsigned long heap_free_bytes;
} SystemExtensionMemInfo;

typedef struct {
    unsigned long version;
    unsigned long tick_msec;
    void (*console_write)(const char* text);
    unsigned long (*get_ticks)(void);
    void (*get_mem_info)(SystemExtensionMemInfo* info);
} SystemExtKernelApi;

static inline const volatile SystemExtKernelApi* system_extension_kernel_api(void) {
    return (const volatile SystemExtKernelApi*)SYSTEM_EXT_API_VA;
}

static inline void system_extension_write(const char* text) {
    const volatile SystemExtKernelApi* api = system_extension_kernel_api();

    if (api && api->version == SYSTEM_EXT_API_VERSION && api->console_write) {
        api->console_write(text);
    }
}

static inline unsigned long system_extension_get_ticks(void) {
    const volatile SystemExtKernelApi* api = system_extension_kernel_api();

    if (!api || api->version != SYSTEM_EXT_API_VERSION || !api->get_ticks) {
        return 0;
    }
    return api->get_ticks();
}

static inline void system_extension_get_mem_info(SystemExtensionMemInfo* info) {
    const volatile SystemExtKernelApi* api = system_extension_kernel_api();

    if (!info) {
        return;
    }
    if (!api || api->version != SYSTEM_EXT_API_VERSION || !api->get_mem_info) {
        info->total_bytes = 0;
        info->free_bytes = 0;
        info->page_size = 0;
        info->free_pages = 0;
        info->heap_total_bytes = 0;
        info->heap_used_bytes = 0;
        info->heap_free_bytes = 0;
        return;
    }
    api->get_mem_info(info);
}

static inline void system_extension_vlog(const char* level, const char* tag, const char* fmt, va_list args) {
    static char line[192];
    int used;
    int written;
    size_t length;

    if (tag && tag[0] != '\0') {
        used = snprintf(line, sizeof(line), "[%s][%s] ", level, tag);
    }
    else {
        used = snprintf(line, sizeof(line), "[%s] ", level);
    }

    if (used < 0) {
        return;
    }
    if ((size_t)used >= sizeof(line)) {
        line[sizeof(line) - 2] = '\n';
        line[sizeof(line) - 1] = '\0';
        system_extension_write(line);
        return;
    }

    written = vsnprintf(line + used, sizeof(line) - (size_t)used, fmt, args);
    if (written < 0) {
        return;
    }

    length = strlen(line);
    if (length + 1 < sizeof(line)) {
        line[length] = '\n';
        line[length + 1] = '\0';
    }
    else {
        line[sizeof(line) - 2] = '\n';
        line[sizeof(line) - 1] = '\0';
    }
    system_extension_write(line);
}

static inline void system_extension_log(const char* level, const char* tag, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    system_extension_vlog(level, tag, fmt, args);
    va_end(args);
}

#define system_extension_log_debug(tag, fmt, ...) system_extension_log("DEBUG", tag, fmt, ##__VA_ARGS__)
#define system_extension_log_info(tag, fmt, ...) system_extension_log("INFO", tag, fmt, ##__VA_ARGS__)
#define system_extension_log_warn(tag, fmt, ...) system_extension_log("WARN", tag, fmt, ##__VA_ARGS__)
#define system_extension_log_error(tag, fmt, ...) system_extension_log("ERROR", tag, fmt, ##__VA_ARGS__)

#endif