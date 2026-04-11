#ifndef KERNEL_INCLUDE_FILESYSTEM_H
#define KERNEL_INCLUDE_FILESYSTEM_H

#include "types.h"

typedef enum FilesystemNodeType {
    FilesystemNodeTypeUnknown = 0,
    FilesystemNodeTypeDirectory = 1,
    FilesystemNodeTypeFile = 2,
    FilesystemNodeTypeDevice = 3,
} FilesystemNodeType;

typedef struct FilesystemNode {
    U64 identifier;
    U64 size_bytes;
    U32 mode;
    FilesystemNodeType type;
    U64 private_data[4];
} FilesystemNode;

typedef struct FilesystemDriver FilesystemDriver;
typedef Status(*FilesystemEnumerateVisitor)(const char* name, const FilesystemNode* node, void* context);

typedef struct FilesystemOps {
    Status(*mount)(void* fs_state, void* device_state);
    Status(*unmount)(void* fs_state);
    Status(*lookup)(void* fs_state, const char* path, FilesystemNode* node);
    Status(*create)(void* fs_state, const char* path, FilesystemNodeType type, FilesystemNode* node);
    Status(*remove)(void* fs_state, const char* path);
    SSize(*read)(void* fs_state, FilesystemNode* node, U64 offset, void* buffer, Size length);
    SSize(*write)(void* fs_state, FilesystemNode* node, U64 offset, const void* buffer, Size length);
    Status(*enumerate)(void* fs_state, FilesystemNode* directory, void* context, FilesystemEnumerateVisitor visitor);
} FilesystemOps;

struct FilesystemDriver {
    const char* name;
    const FilesystemOps* ops;
    void* state;
};

class FilesystemRegistry final {
public:
    static Status init(void);
    static Status register_driver(FilesystemDriver* driver);
};

#endif // KERNEL_INCLUDE_FILESYSTEM_H
