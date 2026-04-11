#ifndef KERNEL_BOOTLOADER_INCLUDE_KERNEL_FORMAT_H
#define KERNEL_BOOTLOADER_INCLUDE_KERNEL_FORMAT_H

#include "types.h"

#define KERNEL_IMAGE_MAGIC_0 'K'
#define KERNEL_IMAGE_MAGIC_1 'E'
#define KERNEL_IMAGE_MAGIC_2 'R'
#define KERNEL_IMAGE_MAGIC_3 'N'
#define KERNEL_IMAGE_MAGIC_4 'E'
#define KERNEL_IMAGE_MAGIC_5 'L'
#define KERNEL_IMAGE_MAGIC_6 '\0'
#define KERNEL_IMAGE_MAGIC_7 '\0'

#define KERNEL_IMAGE_VERSION 1U
#define KERNEL_IMAGE_MACHINE_AARCH64 183U

typedef struct PACKED KernelImageHeader {
    char magic[8];
    U32 version;
    U32 header_size;
    U32 machine;
    U32 flags;
    U64 load_address;
    U64 image_base;
    U64 entry_point;
    U64 payload_offset;
    U64 payload_size;
    U64 memory_size;
    U64 alignment;
} KernelImageHeader;

#endif