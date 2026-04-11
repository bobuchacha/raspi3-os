#ifndef KERNEL_BOOTLOADER_INCLUDE_BOOT_PROTOCOL_H
#define KERNEL_BOOTLOADER_INCLUDE_BOOT_PROTOCOL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_HANDOFF_MAGIC 0x524F53424F4F5401ULL
#define BOOT_HANDOFF_VERSION 1U

    enum BootFlags {
        BootFlagHigherHalf = 1U << 0,
        BootFlagServiceCall = 1U << 1,
        BootFlagHasDtb = 1U << 2,
        BootFlagHasInitrd = 1U << 3,
        BootFlagHasMemoryMap = 1U << 4
    };

    typedef enum BootMemoryType {
        BootMemoryFree = 0,
        BootMemoryReserved = 1,
        BootMemoryFirmware = 2,
        BootMemoryMmio = 3,
        BootMemoryBootloader = 4,
        BootMemoryKernel = 5
    } BootMemoryType;

    typedef struct BootMemoryRegion {
        PhysAddr base;
        Size length;
        U32 type;
        U32 attributes;
    } BootMemoryRegion;

    typedef struct BootHandoff {
        U64 magic;
        U32 version;
        U32 flags;
        U64 boot_cpu_id;
        PhysAddr kernel_phys_base;
        VirtAddr kernel_virt_base;
        VirtAddr kernel_entry;
        PhysAddr dtb_phys;
        PhysAddr initrd_phys;
        Size initrd_size;
        PhysAddr memory_map_phys;
        Size memory_map_size;
    } BootHandoff;

#ifdef __cplusplus
}
#endif

#endif
