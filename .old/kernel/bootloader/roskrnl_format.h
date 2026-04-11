#ifndef ROS_KERNEL_LOADER_FORMAT_H
#define ROS_KERNEL_LOADER_FORMAT_H

#include "ros.h"

#define ROSKRNL_MAGIC_0 'R'
#define ROSKRNL_MAGIC_1 'O'
#define ROSKRNL_MAGIC_2 'S'
#define ROSKRNL_MAGIC_3 'K'
#define ROSKRNL_MAGIC_4 'R'
#define ROSKRNL_MAGIC_5 'N'
#define ROSKRNL_MAGIC_6 'L'
#define ROSKRNL_MAGIC_7 '\0'

#define ROSKRNL_VERSION 1U
#define ROSKRNL_MACHINE_AARCH64 183U

typedef struct __attribute__((packed)) {
    char magic[8];
    UInt version;
    UInt header_size;
    UInt machine;
    UInt flags;
    ULong load_address;
    ULong image_base;
    ULong entry_point;
    ULong payload_offset;
    ULong payload_size;
    ULong memory_size;
    ULong alignment;
} RosKernelHeader;

#endif