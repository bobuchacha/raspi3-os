#ifndef RASPI3_OS_USER_EXE_H
#define RASPI3_OS_USER_EXE_H

#include "ros.h"

#define USER_EXE_MAGIC "ROSXEXE"
#define USER_EXE_VERSION 1U
#define USER_EXE_HEADER_SIZE 512U
#define USER_EXE_SEGMENT_TABLE_OFFSET 56U
#define USER_EXE_MAX_SEGMENTS 8U

#define USER_SHARED_LIBRARY_BASE 0x200000UL
#define USER_SHARED_LIBRARY_LIMIT 0x800000UL
#define USER_SHARED_LIBRARY_PATH_MAX 96U
#define USER_SHARED_LIBRARY_MAX_LOADED 16U
#define USER_SHARED_LIBRARY_MAX_PAGES 64U
#define USER_SHARED_LIBRARY_MAX_EXPORTS 32U
#define USER_SHARED_LIBRARY_EXPORT_NAME_MAX 64U
#define USER_SHARED_LIBRARY_MAX_IMPORT_REFS 64U
#define USER_SHARED_LIBRARY_LOCAL_BASE 0x800000UL
#define USER_SHARED_LIBRARY_LOCAL_LIMIT 0xC00000UL
#define USER_SHARED_INPUT_VIEW_BASE 0xC00000UL
#define USER_SHARED_INPUT_VIEW_LIMIT 0xC10000UL
#define USER_SHARED_LIBRARY_MAX_TASK_LOCALS 4U
#define USER_EXEC_PATH_MAX 128U

typedef enum {
    USER_EXE_SEGMENT_READ = 0x1,
    USER_EXE_SEGMENT_WRITE = 0x2,
    USER_EXE_SEGMENT_EXEC = 0x4,
} UserExeSegmentFlags;

typedef enum {
    USER_DLL_SEC_TEXT = 0,
    USER_DLL_SEC_RODATA = 1,
    USER_DLL_SEC_DATA = 2,
} UserDllSectionType;

typedef enum {
    USER_DLL_RELOC_ABS64 = 0,
    USER_DLL_RELOC_REL64 = 1,
    USER_DLL_RELOC_SECTION = 2,
} UserDllRelocType;

typedef struct __attribute__((packed)) {
    ULong file_offset;
    ULong virtual_address;
    ULong file_size;
    ULong memory_size;
    ULong alignment;
    UInt flags;
    UInt reserved;
} UserExeSegment;

typedef struct __attribute__((packed)) {
    char magic[8];
    UInt version;
    UInt header_size;
    ULong entry_point;
    ULong segment_table_offset;
    UInt segment_count;
    UInt flags;
    ULong image_base;
    ULong image_size;
    UserExeSegment segments[USER_EXE_MAX_SEGMENTS];
    UByte reserved[USER_EXE_HEADER_SIZE - USER_EXE_SEGMENT_TABLE_OFFSET -
        (sizeof(UserExeSegment) * USER_EXE_MAX_SEGMENTS)];
} UserExeHeader;

typedef char UserExeHeaderSizeCheck[(sizeof(UserExeHeader) == USER_EXE_HEADER_SIZE) ? 1 : -1];

typedef struct {
    Bool used;
    char path[USER_SHARED_LIBRARY_PATH_MAX];
    Address base_va;
    Address entry_point;
    ULong image_size;
    UInt page_count;
    UInt ref_count;
    UInt kind;
    Bool driver_loop_active;
} UserSharedLibraryInfo;

typedef struct {
    char name[USER_SHARED_LIBRARY_EXPORT_NAME_MAX];
    Address address;
} UserSharedLibraryExportInfo;

typedef struct {
    char importer_path[USER_SHARED_LIBRARY_PATH_MAX];
    char symbol_name[USER_SHARED_LIBRARY_EXPORT_NAME_MAX];
    Address iat_address;
} UserSharedLibraryImportInfo;

int exec_user_program(const char* path);
int spawn_user_program(const char* path, const char* name, const char* args);
Address load_user_shared_library(const char* path);
long unload_user_shared_library(const char* path);
long unload_user_driver(const char* path);
Address load_user_shared_library_export(const char* path, const char* export_name);
Address load_user_shared_library_local(const char* path, ULong size);
UInt user_shared_library_snapshot(UserSharedLibraryInfo* infos, UInt max_infos);
UInt user_shared_library_export_snapshot(const char* path, UserSharedLibraryExportInfo* exports, UInt max_exports);
UInt user_shared_library_import_snapshot(long task_id, const char* path, UserSharedLibraryImportInfo* imports, UInt max_imports);

#endif
