#ifndef RASPI3_OS_SYSTEM_EXT_H
#define RASPI3_OS_SYSTEM_EXT_H

#include "ros.h"

#define SYSTEM_EXT_API_VERSION 1U
#define SYSTEM_EXT_API_VA 0xBFF000UL
#define SYSTEM_EXT_TICK_MSEC 500UL

typedef struct {
    ULong total_bytes;
    ULong free_bytes;
    ULong page_size;
    ULong free_pages;
    ULong heap_total_bytes;
    ULong heap_used_bytes;
    ULong heap_free_bytes;
} SystemExtMemInfo;

typedef struct {
    ULong version;
    ULong tick_msec;
    void (*console_write)(const char* text);
    ULong(*get_ticks)(void);
    void (*get_mem_info)(SystemExtMemInfo* info);
} SystemExtKernelApi;

int register_system_extension_from_path(const char* path);
void system_extensions_run_idle_loops(void);
long extension_call_by_name(const char* ext_name, const char* func_name, unsigned long a, unsigned long b);

#endif
