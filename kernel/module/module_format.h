#ifndef RASPI3_OS_MODULE_FORMAT_H
#define RASPI3_OS_MODULE_FORMAT_H

#include "ros.h"

#define MOD_MAGIC_0 'R'
#define MOD_MAGIC_1 'O'
#define MOD_MAGIC_2 'S'
#define MOD_MAGIC_3 'M'
#define MOD_MAGIC_4 'O'
#define MOD_MAGIC_5 'D'
#define MOD_MAGIC_6 '\0'
#define MOD_MAGIC_7 '\0'
#define MOD_ABI_VERSION 1U
#define MOD_MACHINE_AARCH64 0xB7U
#define MOD_INVALID_SECTION 0xFFFFFFFFU

#define MOD_SECTION_FLAG_READ 0x1U
#define MOD_SECTION_FLAG_WRITE 0x2U
#define MOD_SECTION_FLAG_EXEC 0x4U

#define MOD_IMPORT_ABS64 0U
#define MOD_IMPORT_REL64 1U

#define MODULE_BUNDLE_MAGIC_0 'R'
#define MODULE_BUNDLE_MAGIC_1 'O'
#define MODULE_BUNDLE_MAGIC_2 'S'
#define MODULE_BUNDLE_MAGIC_3 'K'
#define MODULE_BUNDLE_MAGIC_4 'M'
#define MODULE_BUNDLE_MAGIC_5 'O'
#define MODULE_BUNDLE_MAGIC_6 'D'
#define MODULE_BUNDLE_MAGIC_7 '\0'
#define MODULE_BUNDLE_VERSION 1U
#define MODULE_BUNDLE_HEADER_SIZE 256U
#define MODULE_BUNDLE_NAME_SIZE 64U
#define MODULE_BUNDLE_EXPORT_NAME_SIZE 16U
#define MODULE_BUNDLE_MAX_EXPORTS 4U

/* Identifies the canonical section classes emitted into the flat module image. */
typedef enum {
    MOD_SEC_TEXT = 0,
    MOD_SEC_RODATA = 1,
    MOD_SEC_DATA = 2,
} ModuleSectionType;

/* Enumerates relocation encodings supported by the flat runtime loader. */
typedef enum {
    MOD_RELOC_ABS64 = 0,
    MOD_RELOC_REL64 = 1,
    MOD_RELOC_SECTION = 2,
} ModuleRelocType;

/* Describes the flat image payload produced from the intermediate ELF. */
typedef struct __attribute__((packed)) {
    char magic[8];
    UWord abi_version;
    UWord machine;
    UInt flags;
    UInt header_size;
    UInt section_count;
    UInt import_count;
    UInt reloc_count;
    UInt entry_section;
    UInt reserved0;
    ULong entry_offset;
    ULong image_size;
    ULong bss_size;
    ULong align;
} ModuleHeader;

/* Describes one loadable section inside the flat module payload. */
typedef struct __attribute__((packed)) {
    UInt type;
    UInt flags;
    ULong runtime_offset;
    ULong file_offset;
    ULong file_size;
    ULong mem_size;
    ULong align;
} ModuleSection;

/* Describes one imported kernel symbol that must be patched by name. */
typedef struct __attribute__((packed)) {
    UInt name_offset;
    UInt type;
    ULong patch_offset;
} ModuleImport;

/* Describes one relocation record applied against a loaded section. */
typedef struct __attribute__((packed)) {
    UInt type;
    UInt section;
    ULong offset;
    ULong addend;
} ModuleReloc;

/* Declares one user-callable export published from a module bundle. */
typedef struct __attribute__((packed)) {
    char name[MODULE_BUNDLE_EXPORT_NAME_SIZE];
    UInt section;
    ULong offset;
} ModuleBundleExport;

/* Wraps the flat payload with lifecycle metadata and export declarations. */
typedef struct __attribute__((packed)) {
    char magic[8];
    UInt header_size;
    UWord version;
    UWord abi_version;
    UInt flags;
    UInt manifest_size;
    UInt module_size;
    UInt payload_align;
    char name[MODULE_BUNDLE_NAME_SIZE];
    UInt init_section;
    UInt shutdown_section;
    UInt idle_section;
    UInt reserved0;
    ULong init_offset;
    ULong shutdown_offset;
    ULong idle_offset;
    UInt export_count;
    UInt reserved1;
    ModuleBundleExport exports[MODULE_BUNDLE_MAX_EXPORTS];
} ModuleBundleHeader;

#endif
