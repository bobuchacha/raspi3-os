#ifndef ROS_APP_FS_MODULE_H
#define ROS_APP_FS_MODULE_H

#include "app/import.h"
#include "stdint.h"

#define FS_MODULE_NAME "fs"
#define FS_MODULE_EXPORT_CALL "call"
#define FS_MODULE_EXPORT_QUERY "query"

enum {
    FS_MODULE_QUERY_ABI_VERSION = 0,
    FS_MODULE_QUERY_BACKEND_KIND = 1,
    FS_MODULE_QUERY_FEATURES = 2,
};

enum {
    FS_MODULE_BACKEND_NONE = 0,
    FS_MODULE_BACKEND_FAT32 = 1,
};

enum {
    FS_MODULE_FEATURE_READ_FILE = 0x1,
    FS_MODULE_FEATURE_LIST_DIR = 0x2,
    FS_MODULE_FEATURE_WRITE_FILE = 0x4,
    FS_MODULE_FEATURE_MKDIR = 0x8,
    FS_MODULE_FEATURE_REMOVE = 0x10,
};

enum {
    FS_MODULE_OP_READ_FILE = 1,
    FS_MODULE_OP_WRITE_FILE = 2,
    FS_MODULE_OP_DIR_ENTRY = 3,
    FS_MODULE_OP_MKDIR = 4,
    FS_MODULE_OP_REMOVE = 5,
};

enum {
    FS_MODULE_WRITE_TRUNCATE = 0x1,
    FS_MODULE_WRITE_APPEND = 0x2,
};

typedef struct FsModuleReadFileRequest {
    const char* path;
    uint64_t offset;
    char* buffer;
    uint64_t size;
} FsModuleReadFileRequest;

typedef struct FsModuleWriteFileRequest {
    const char* path;
    const char* buffer;
    uint64_t size;
    uint32_t flags;
} FsModuleWriteFileRequest;

typedef struct FsModuleDirEntryRequest {
    const char* path;
    uint64_t entry_index;
    UserDirectoryEntry* entry;
} FsModuleDirEntryRequest;

typedef struct FsModulePathRequest {
    const char* path;
} FsModulePathRequest;

static inline long fs_module_query(unsigned long query) {
    return user_kernel_extension_invoke(FS_MODULE_NAME, FS_MODULE_EXPORT_QUERY, query, 0);
}

static inline long fs_module_read_file(const char* path, unsigned long offset, char* buffer, unsigned long size) {
    FsModuleReadFileRequest request = { path, offset, buffer, size };
    return user_kernel_extension_invoke(FS_MODULE_NAME, FS_MODULE_EXPORT_CALL, FS_MODULE_OP_READ_FILE, (unsigned long)&request);
}

static inline long fs_module_write_file(const char* path, const char* buffer, unsigned long size, unsigned long flags) {
    FsModuleWriteFileRequest request = { path, buffer, size, (uint32_t)flags };
    return user_kernel_extension_invoke(FS_MODULE_NAME, FS_MODULE_EXPORT_CALL, FS_MODULE_OP_WRITE_FILE, (unsigned long)&request);
}

static inline long fs_module_dir_entry(const char* path, unsigned long entry_index, UserDirectoryEntry* entry) {
    FsModuleDirEntryRequest request = { path, entry_index, entry };
    return user_kernel_extension_invoke(FS_MODULE_NAME, FS_MODULE_EXPORT_CALL, FS_MODULE_OP_DIR_ENTRY, (unsigned long)&request);
}

static inline long fs_module_mkdir(const char* path) {
    FsModulePathRequest request = { path };
    return user_kernel_extension_invoke(FS_MODULE_NAME, FS_MODULE_EXPORT_CALL, FS_MODULE_OP_MKDIR, (unsigned long)&request);
}

static inline long fs_module_remove(const char* path) {
    FsModulePathRequest request = { path };
    return user_kernel_extension_invoke(FS_MODULE_NAME, FS_MODULE_EXPORT_CALL, FS_MODULE_OP_REMOVE, (unsigned long)&request);
}

#endif
