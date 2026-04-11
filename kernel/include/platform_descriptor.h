#ifndef KERNEL_INCLUDE_PLATFORM_DESCRIPTOR_H
#define KERNEL_INCLUDE_PLATFORM_DESCRIPTOR_H

#include "types.h"

typedef struct PlatformDescriptor {
    const char* board_name;
    const char* arch_name;
    const char* cpu_name;
    PhysAddr dram_base;
    Size dram_size;
} PlatformDescriptor;

#endif // KERNEL_INCLUDE_PLATFORM_DESCRIPTOR_H