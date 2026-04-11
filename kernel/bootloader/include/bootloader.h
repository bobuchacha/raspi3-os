#ifndef KERNEL_BOOTLOADER_INCLUDE_BOOTLOADER_H
#define KERNEL_BOOTLOADER_INCLUDE_BOOTLOADER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct BootTarget {
        const char* board_name;
        const char* kernel_path;
        PhysAddr load_phys_base;
    } BootTarget;

    void boot_main(void);
    const BootTarget* boot_target(void);

#ifdef __cplusplus
}
#endif

#endif
