#ifndef ROS_LOADER_MEMORY_H
#define ROS_LOADER_MEMORY_H

#define VA_START 0xFFFF000000000000UL
#define LOW_MEMORY_CEILING 0x800000UL
#define PAGE_SIZE 0x1000UL

#ifndef __ASSEMBLER__

#include "ros.h"

typedef ULong PhysAddr;
typedef ULong VirtAddr;

static inline VirtAddr mem_phys_to_virt(PhysAddr phys_addr) {
    return (VirtAddr)(phys_addr + VA_START);
}

static inline Bool mem_is_page_aligned(ULong addr) {
    return (addr & (PAGE_SIZE - 1UL)) == 0;
}

#endif

#endif
