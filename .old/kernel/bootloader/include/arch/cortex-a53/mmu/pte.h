#ifndef ROS_LOADER_PTE_H
#define ROS_LOADER_PTE_H

#include "memory.h"
#include "arch/cortex-a53/mmu/mair.h"

#define pentry_t unsigned long long

#define PT_BLOCK_ENTRY ((pentry_t)0x1)
#define PT_TABLE_ENTRY ((pentry_t)0x3)

#define PE_AP_KERNEL_RO ((pentry_t)2 << 6)
#define PE_AP_KERNEL_RW ((pentry_t)0 << 6)
#define PE_OSH ((pentry_t)2 << 8)
#define PE_ISH ((pentry_t)3 << 8)
#define PE_ACCESSED ((pentry_t)1 << 10)
#define PE_PXN ((pentry_t)1 << 53)
#define PE_UXN ((pentry_t)1 << 54)

#define PE_DEVICE ((((pentry_t)MAIR_DEVICE_INDEX << 2) | PE_AP_KERNEL_RW | PE_PXN | PE_UXN | PE_OSH) | PE_ACCESSED)
#define PE_KERNEL_CODE ((((pentry_t)MAIR_CACHEABLE_INDEX << 2) | PE_AP_KERNEL_RW | PE_UXN | PE_ISH) | PE_ACCESSED)

#endif
