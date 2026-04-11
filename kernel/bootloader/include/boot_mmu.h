#ifndef KERNEL_BOOTLOADER_INCLUDE_BOOT_MMU_H
#define KERNEL_BOOTLOADER_INCLUDE_BOOT_MMU_H

#include "types.h"

#if defined(ARCH_AARCH64) || defined(__aarch64__)
#include "boot_mmu/arch/aarch64/boot_mmu_backend.h"
#else
#include "boot_mmu/arch/aarch64/boot_mmu_backend.h"
#endif

#if defined(BOARD_RASPI3)
#include "boot_mmu/board/raspi3/boot_mmu_board.h"
#elif defined(BOARD_VIRT)
#include "boot_mmu/board/virt/boot_mmu_board.h"
#else
#include "boot_mmu/board/virt/boot_mmu_board.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

    void boot_enable_mmu(void);

#ifdef __cplusplus
}
#endif

#define PAGE_ALIGNED __attribute__((aligned(4096)))

#endif // KERNEL_BOOTLOADER_INCLUDE_BOOT_MMU_H