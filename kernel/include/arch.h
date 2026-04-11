#ifndef KERNEL_INCLUDE_ARCH_H
#define KERNEL_INCLUDE_ARCH_H

#include "types.h"

typedef struct ArchBootInfo {
    PhysAddr dtb_phys;
    PhysAddr initrd_phys;
    Size initrd_size;
    U64 boot_cpu_id;
} ArchBootInfo;

#if !defined(__cplusplus)
#error "arch.h requires C++"
#endif

#if defined(ARCH_AARCH64) || defined(__aarch64__)
#include "arch/aarch64/arch_backend.h"
#else
#include "arch/aarch64/arch_backend.h"
#endif

#endif // KERNEL_INCLUDE_ARCH_H
