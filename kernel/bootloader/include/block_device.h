#ifndef KERNEL_BOOTLOADER_INCLUDE_BLOCK_DEVICE_H
#define KERNEL_BOOTLOADER_INCLUDE_BLOCK_DEVICE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

    Status boot_block_init(void);
    Status boot_block_read(U32 lba, U32 sector_count, void* buffer);
    bool boot_block_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif