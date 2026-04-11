#ifndef RASPI3_OS_MODULE_H
#define RASPI3_OS_MODULE_H

#include "filesystem/vfs/vfs.h"
#include "ros.h"

#define MODULE_REGION_BASE 0xFFFF000010000000UL
#define MODULE_REGION_SIZE 0x01000000UL
#define MODULE_REGION_LIMIT (MODULE_REGION_BASE + MODULE_REGION_SIZE)
#define MODULE_TICK_MSEC 500UL
#define MODULE_MAX_COUNT 16U
#define MODULE_NAME_MAX 64U
#define MODULE_PATH_MAX 128U
#define MODULE_EXPORT_NAME_MAX 64U
#define MODULE_BUNDLE_MAX_EXPORTS 16U

#define MODULE_BUNDLE_MAGIC_0 'R'
#define MODULE_BUNDLE_MAGIC_1 'O'
#define MODULE_BUNDLE_MAGIC_2 'S'
#define MODULE_BUNDLE_MAGIC_3 'M'
#define MODULE_BUNDLE_MAGIC_4 'O'
#define MODULE_BUNDLE_MAGIC_5 'D'
#define MODULE_BUNDLE_MAGIC_6 '\0'
#define MODULE_BUNDLE_MAGIC_7 '\0'
#define MODULE_BUNDLE_HEADER_SIZE ((UInt)sizeof(ModuleBundleHeader))
#define MODULE_BUNDLE_VERSION 1U

#define MOD_MAGIC_0 'R'
#define MOD_MAGIC_1 'O'
#define MOD_MAGIC_2 'S'
#define MOD_MAGIC_3 'M'
#define MOD_MAGIC_4 'O'
#define MOD_MAGIC_5 'D'
#define MOD_MAGIC_6 '\0'
#define MOD_MAGIC_7 '\0'
#define MOD_ABI_VERSION 1U
#define MOD_MACHINE_AARCH64 183U

#define MOD_SEC_TEXT 0U
#define MOD_SEC_RODATA 1U
#define MOD_SEC_DATA 2U

#define MOD_SECTION_FLAG_READ  (1U << 0)
#define MOD_SECTION_FLAG_WRITE (1U << 1)
#define MOD_SECTION_FLAG_EXEC  (1U << 2)

#define MOD_IMPORT_ABS64 1U
#define MOD_IMPORT_REL64 2U
#define MOD_INVALID_SECTION 0xFFFFFFFFU

typedef enum ModuleState {
    MODULE_STATE_DISCOVERED = 0,
    MODULE_STATE_READY = 1,
    MODULE_STATE_FAILED = 2,
} ModuleState;

typedef struct __attribute__((packed)) ModuleExportRecord {
    char name[MODULE_EXPORT_NAME_MAX];
    UInt section;
    ULong offset;
} ModuleExportRecord;

typedef struct __attribute__((packed)) ModuleBundleHeader {
    char magic[8];
    UInt header_size;
    UInt version;
    UInt abi_version;
    UInt payload_align;
    UInt manifest_size;
    UInt module_size;
    char name[MODULE_NAME_MAX];
    UInt init_section;
    ULong init_offset;
    UInt shutdown_section;
    ULong shutdown_offset;
    UInt idle_section;
    ULong idle_offset;
    UInt export_count;
    ModuleExportRecord exports[MODULE_BUNDLE_MAX_EXPORTS];
} ModuleBundleHeader;

typedef struct __attribute__((packed)) ModuleHeader {
    char magic[8];
    UInt abi_version;
    UInt machine;
    UInt header_size;
    UInt section_count;
    UInt import_count;
    ULong align;
    ULong image_size;
} ModuleHeader;

typedef struct __attribute__((packed)) ModuleSection {
    UInt type;
    UInt flags;
    ULong runtime_offset;
    ULong file_offset;
    ULong file_size;
    ULong mem_size;
    ULong align;
} ModuleSection;

typedef struct __attribute__((packed)) ModuleImport {
    UInt type;
    UInt name_offset;
    ULong patch_offset;
} ModuleImport;

typedef struct KernelModuleInfo {
    Bool used;
    char name[MODULE_NAME_MAX];
    char path[MODULE_PATH_MAX];
    ModuleState state;
    UInt flags;
    ULong image_size;
    ULong bss_size;
} KernelModuleInfo;

typedef struct KernelModuleRuntimeInfo {
    Bool ready;
    Address base_va;
    Address load_bias;
    Address init_va;
    Address shutdown_va;
    Address idle_va;
    ULong image_size;
    ULong bss_size;
    UInt export_count;
} KernelModuleRuntimeInfo;

typedef struct KernelModuleExportInfo {
    char name[MODULE_EXPORT_NAME_MAX];
    Address address;
} KernelModuleExportInfo;

void module_subsystem_init(void);
void module_load_boot_modules(void);
void module_run_idle_loops(void);
int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);
int module_read_header(struct FileDesc* fd, ModuleBundleHeader* header);
int module_validate_header(const ModuleBundleHeader* header);
const KernelModuleInfo* module_find(const char* name);
const KernelModuleInfo* module_get_at(unsigned int index);
unsigned int module_count(void);
unsigned int module_export_count(const char* name);
int module_export_get(const char* name, unsigned int index, KernelModuleExportInfo* info);
int module_runtime_get(const char* name, KernelModuleRuntimeInfo* runtime);

#endif
