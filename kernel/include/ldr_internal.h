/*
 * ldr_internal.h
 *
 * Internal structures and module-private function declarations.
 */
#ifndef LDR_INTERNAL_H
#define LDR_INTERNAL_H

#include "ldr_loader.h"

typedef struct LdrSectionStruct {
    CONST char* name;
    Flags flags;
    Offset rva;
    Offset fileOffset;
    Size fileSize;
    Size virtualSize;
} LDR_SECTION;

typedef LDR_SECTION* PLDR_SECTION;
typedef CONST LDR_SECTION* PCLDR_SECTION;

typedef struct LdrImportStruct {
    CONST char* moduleName;
    CONST char* symbolName;
    Offset iatRva;
} LDR_IMPORT;

typedef LDR_IMPORT* PLDR_IMPORT;
typedef CONST LDR_IMPORT* PCLDR_IMPORT;

typedef struct LdrExportStruct {
    CONST char* symbolName;
    Offset symbolRva;
} LDR_EXPORT;

typedef LDR_EXPORT* PLDR_EXPORT;
typedef CONST LDR_EXPORT* PCLDR_EXPORT;

typedef struct LdrRelocStruct {
    Offset patchRva;
    UInt type;
    Long addend;
} LDR_RELOC;

typedef LDR_RELOC* PLDR_RELOC;
typedef CONST LDR_RELOC* PCLDR_RELOC;

struct LdrModuleStruct {
    LDR_IMAGEKIND kind;
    CONST char* path;
    Size imageSize;
    Address preferredBase;
    Offset entryRva;

    Address baseAddress;
    LDR_PROCESSHANDLE ownerProcess;

    PLDR_SECTION sections;
    UInt sectionCount;

    PLDR_IMPORT imports;
    UInt importCount;

    PLDR_EXPORT exports;
    UInt exportCount;

    PLDR_RELOC relocs;
    UInt relocCount;

    Int referenceCount;
    Bool initCalled;
    Bool deinitCalled;
    Bool isKernelModule;

    PLDR_MODULE nextModule;
};

struct LdrContextStruct {
    PCLDR_KERNELAPI api;
    LDR_LOCKHANDLE graphLock;
    PLDR_MODULE moduleHead;
};

/* Shared helpers across implementation units. */
LDR_RESULT ldr_parse_image(PLDR_CONTEXT context, CONST UByte* buffer, Size size, CONST char* path, PLDR_MODULE* outModule);
LDR_RESULT ldr_vm_map_image(PLDR_CONTEXT context, PLDR_MODULE module);
LDR_RESULT ldr_sections_load(PLDR_CONTEXT context, PLDR_MODULE module, CONST UByte* buffer, Size size);
LDR_RESULT ldr_reloc_apply(PLDR_CONTEXT context, PLDR_MODULE module);
LDR_RESULT ldr_imports_bind(PLDR_CONTEXT context, PLDR_MODULE module);
Int ldr_write_module_bytes(PLDR_MODULE module, Address destinationAddress, CONST void* sourceBuffer, Size byteCount);
Int ldr_zero_module_bytes(PLDR_MODULE module, Address destinationAddress, Size byteCount);
Int ldr_write_module_u64(PLDR_MODULE module, Address destinationAddress, ULong value);
LDR_RESULT ldr_module_register(PLDR_CONTEXT context, PLDR_MODULE module);
LDR_RESULT ldr_module_unregister(PLDR_CONTEXT context, PLDR_MODULE module);
LDR_RESULT ldr_process_prepare_exe(PLDR_CONTEXT context, PLDR_MODULE module, PCLDR_LOADREQUEST request);
LDR_RESULT ldr_driver_finalize_load(PLDR_CONTEXT context, PLDR_MODULE module);

#endif /* LDR_INTERNAL_H */
