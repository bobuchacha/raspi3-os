#ifndef RASPI3_OS_MODULE_H
#define RASPI3_OS_MODULE_H

#include "ros.h"

#define MODULE_REGION_BASE 0xFFFF000010000000UL
#define MODULE_REGION_SIZE 0x01000000UL
#define MODULE_REGION_LIMIT (MODULE_REGION_BASE + MODULE_REGION_SIZE)
#define MODULE_TICK_MSEC 500UL
#define MODULE_MAX_COUNT 16U
#define MODULE_MAX_NAME 64U
#define MODULE_EXPORT_NAME_MAX 16U

typedef enum {
    MODULE_STATE_EMPTY = 0,
    MODULE_STATE_DISCOVERED = 1,
    MODULE_STATE_READY = 2,
    MODULE_STATE_FAILED = 3,
} ModuleState;

typedef struct {
    Bool used;
    ModuleState state;
    char name[MODULE_MAX_NAME];
    char path[128];
    unsigned int flags;
    unsigned long image_size;
    unsigned long bss_size;
} KernelModuleInfo;

typedef struct {
    char name[MODULE_EXPORT_NAME_MAX];
    Address address;
} KernelModuleExportInfo;

typedef struct {
    Bool ready;
    Address base_va;
    Address load_bias;
    Address init_va;
    Address shutdown_va;
    Address idle_va;
    unsigned long image_size;
    unsigned long bss_size;
    unsigned int export_count;
} KernelModuleRuntimeInfo;

void module_subsystem_init(void);
void module_load_boot_modules(void);
void module_run_idle_loops(void);
Address module_vm_reserve(ULong size, ULong align);
int module_vm_map_page(Address va, Address pa, Flags flags);
int module_vm_unmap_page(Address va);
int module_vm_update_page_flags(Address va, Flags flags);
void module_vm_sync_icache(Address start_va, ULong size);
const KernelModuleInfo* module_find(const char* name);
const KernelModuleInfo* module_get_at(unsigned int index);
unsigned int module_count(void);
int module_runtime_get(const char* module_name, KernelModuleRuntimeInfo* out);
unsigned int module_export_count(const char* module_name);
int module_export_get(const char* module_name, unsigned int index, KernelModuleExportInfo* out);
int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);

#endif
