#ifndef KERNEL_INCLUDE_VFS_H
#define KERNEL_INCLUDE_VFS_H

#include "device.h"
#include "filesystem.h"
#include "types.h"

#if !defined(__cplusplus)
#error "vfs.h requires C++"
#endif

#define VFS_DRIVE_LETTER_FIRST 'C'
#define VFS_DRIVE_LETTER_LAST 'Z'
#define VFS_VOLUME_SLOT_COUNT 26U
#define VFS_DEVICE_NAME_CAPACITY 16U
#define VFS_DEVICE_ALIAS_CAPACITY 32U
#define VFS_PATH_CAPACITY 260U

typedef enum VfsNodeType {
    VfsNodeTypeUnknown = 0,
    VfsNodeTypeDirectory = 1,
    VfsNodeTypeFile = 2,
    VfsNodeTypeDevice = 3,
} VfsNodeType;

typedef enum VfsBackendKind {
    VfsBackendKindNone = 0,
    VfsBackendKindFilesystem = 1,
    VfsBackendKindDevice = 2,
} VfsBackendKind;

typedef struct VfsNode VfsNode;
typedef Status(*VfsEnumerateVisitor)(const char* name, const VfsNode* node, void* context);

typedef struct VfsMount {
    char drive_letter;
    FilesystemDriver* filesystem;
    void* filesystem_state;
    void* device_state;
} VfsMount;

struct VfsNode {
    U64 inode;
    U64 size_bytes;
    VfsNodeType type;
    U32 flags;
    VfsBackendKind backend_kind;
    char volume_letter;
    char device_name[VFS_DEVICE_NAME_CAPACITY];
    FilesystemDriver* filesystem;
    void* filesystem_state;
    Device* device;
    FilesystemNode backing_node;
};

class VirtualFileSystem final {
public:
    static Status init(void);
    static Status mount_root(const VfsMount* mount);
    static Status mount_volume(const VfsMount* mount);
    static Status register_device_name(const char* name, Device* device);
    static Status resolve(const char* path, VfsNode* node);
    static Status create(const char* path, VfsNodeType type, VfsNode* node = NULL);
    static Status remove(const char* path);
    static SSize read(const VfsNode* node, U64 offset, void* buffer, Size length);
    static SSize write(VfsNode* node, U64 offset, const void* buffer, Size length);
    static Status enumerate(const VfsNode* node, void* context, VfsEnumerateVisitor visitor);
};

#endif // KERNEL_INCLUDE_VFS_H
