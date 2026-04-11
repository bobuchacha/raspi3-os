#ifndef KERNEL_INCLUDE_DLL_IMAGE_H
#define KERNEL_INCLUDE_DLL_IMAGE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    /*
     * Windows-like image container used for user EXEs, DLLs, and future SYS modules.
     *
     * The format intentionally keeps the PE concepts that matter to the loader:
     * preferred image base, RVA-based sections, import/export tables, relocation
     * records, and one entry point. The binary layout is smaller than Microsoft PE
     * but stays close enough that the loader reads like a PE-style image mapper.
     */

#define DLL_IMAGE_MAGIC 0x304C4C44U /* "DLL0" */

    enum {
        DLL_IMAGE_VERSION_MAJOR = 1U,
        DLL_IMAGE_VERSION_MINOR = 0U,
    };

    enum {
        DLL_IMAGE_TYPE_EXE = 1U,
        DLL_IMAGE_TYPE_DLL = 2U,
        DLL_IMAGE_TYPE_DRIVER = 3U,
    };

    enum {
        DLL_MACHINE_X86 = 0x014CU,
        DLL_MACHINE_X64 = 0x8664U,
        DLL_MACHINE_AARCH64 = 0xAA64U,
    };

    enum {
        DLL_SEC_READ = 1U << 0,
        DLL_SEC_WRITE = 1U << 1,
        DLL_SEC_EXEC = 1U << 2,
        DLL_SEC_BSS = 1U << 3,
    };

    enum {
        DLL_RELOC_ABS64 = 1U,
        DLL_RELOC_ABS32 = 2U,
    };

    enum {
        DLL_REASON_PROCESS_DETACH = 0U,
        DLL_REASON_PROCESS_ATTACH = 1U,
        DLL_REASON_THREAD_ATTACH = 2U,
        DLL_REASON_THREAD_DETACH = 3U,
    };

    typedef struct PACKED dll_header {
        U32 magic;
        U16 version_major;
        U16 version_minor;
        U16 machine;
        U16 image_type;
        U32 flags;
        U64 image_base;
        U64 entry_point_rva;
        U64 image_size;
        U64 header_size;
        U64 section_alignment;
        U64 file_alignment;
        U32 section_count;
        U32 section_table_offset;
        U32 import_module_table_offset;
        U32 import_module_count;
        U32 import_symbol_table_offset;
        U32 import_symbol_count;
        U32 export_table_offset;
        U32 export_count;
        U32 relocation_table_offset;
        U32 relocation_count;
        U32 string_table_offset;
        U32 string_table_size;
        U32 checksum;
        U32 reserved;
    } dll_header;

    typedef struct PACKED dll_section {
        char name[8];
        U64 virtual_address;
        U64 virtual_size;
        U64 raw_data_offset;
        U64 raw_data_size;
        U32 flags;
        U32 reserved;
    } dll_section;

    typedef struct PACKED dll_import_module {
        U32 module_name_offset;
        U32 first_symbol_index;
        U32 symbol_count;
        U32 reserved;
    } dll_import_module;

    typedef struct PACKED dll_import_symbol {
        U32 symbol_name_offset;
        U32 iat_rva;
        U32 flags;
        U32 reserved;
    } dll_import_symbol;

    typedef struct PACKED dll_export_symbol {
        U32 symbol_name_offset;
        U32 flags;
        U64 symbol_rva;
    } dll_export_symbol;

    typedef struct PACKED dll_relocation {
        U32 type;
        U32 reserved;
        U64 target_rva;
    } dll_relocation;

    typedef int (*dll_entry_fn)(void* image_base, U32 reason);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_INCLUDE_DLL_IMAGE_H */