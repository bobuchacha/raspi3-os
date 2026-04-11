#ifndef KERNEL_INTERNAL_VFS_VNODE_H
#define KERNEL_INTERNAL_VFS_VNODE_H

#include "filesystem.h"
#include "vfs.h"

typedef struct Vnode {
    VfsNode public_node;
    FilesystemNode backing_node;
    void* mount;
} Vnode;

#endif // KERNEL_INTERNAL_VFS_VNODE_H
