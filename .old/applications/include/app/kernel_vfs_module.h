#ifndef ROS_APP_KERNEL_VFS_MODULE_H
#define ROS_APP_KERNEL_VFS_MODULE_H

#include "stdint.h"

typedef struct RosKernelVfsPath {
    int parts;
    char* pathbuf;
} RosKernelVfsPath;

typedef struct RosKernelFileDesc {
    unsigned int used : 1;
    unsigned int read : 1;
    unsigned int write : 1;
    unsigned int append : 1;
    unsigned int dir : 1;

    unsigned int fs_id;
    unsigned int block;
    unsigned int offset;
    unsigned int size;
    RosKernelVfsPath path;
} RosKernelFileDesc;

enum {
    ROS_KERNEL_VFS_O_READ = 1,
    ROS_KERNEL_VFS_O_WRITE = 2,
    ROS_KERNEL_VFS_O_CREATE = 4,
    ROS_KERNEL_VFS_O_APPEND = 8,
    ROS_KERNEL_VFS_O_TRUNC = 16,
};

enum {
    ROS_KERNEL_VFS_SEEK_SET = 0,
    ROS_KERNEL_VFS_SEEK_CUR = 1,
    ROS_KERNEL_VFS_SEEK_END = 2,
};

#endif
