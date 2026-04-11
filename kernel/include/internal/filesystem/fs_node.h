#ifndef KERNEL_INTERNAL_FILESYSTEM_FS_NODE_H
#define KERNEL_INTERNAL_FILESYSTEM_FS_NODE_H

#include "filesystem.h"

typedef struct InternalFsNode {
    FilesystemNode public_node;
    void* private_state;
} InternalFsNode;

#endif // KERNEL_INTERNAL_FILESYSTEM_FS_NODE_H
