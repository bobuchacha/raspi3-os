#ifndef KERNEL_INCLUDE_MM_ARCH_AARCH64_MM_BACKEND_H
#define KERNEL_INCLUDE_MM_ARCH_AARCH64_MM_BACKEND_H

#include "internal/mm/arch/aarch64/mmu_defs.h"

namespace mm::backend {

    inline constexpr U64 TableEntries = AARCH64_MMU_TABLE_ENTRIES;  // Number of entries in a translation table
    inline constexpr U64 PageShift = AARCH64_MMU_PAGE_SHIFT;        // Level 3 page shift
    inline constexpr U64 L0Shift = AARCH64_MMU_L0_SHIFT;            // Level 0 table shift
    inline constexpr U64 L1Shift = AARCH64_MMU_L1_SHIFT;            // Level 1 table shift
    inline constexpr U64 L2Shift = AARCH64_MMU_L2_SHIFT;            // Level 2 table shift
    inline constexpr U64 L2BlockSize = AARCH64_MMU_L2_BLOCK_SIZE;   // Level 2 block size
    inline constexpr U64 OutputAddressMask = AARCH64_MMU_OUTPUT_ADDRESS_MASK;
    inline constexpr U64 DescInvalid = AARCH64_MMU_DESC_INVALID;
    inline constexpr U64 DescBlock = AARCH64_MMU_DESC_BLOCK;
    inline constexpr U64 DescTable = AARCH64_MMU_DESC_TABLE;
    inline constexpr U64 DescPage = AARCH64_MMU_DESC_TABLE;
    inline constexpr U64 AttrNonCacheable = AARCH64_MMU_ATTR_NON_CACHEABLE;
    inline constexpr U64 AttrNormal = AARCH64_MMU_ATTR_NORMAL;
    inline constexpr U64 ApKernelRw = AARCH64_MMU_AP_KERNEL_RW;
    inline constexpr U64 ApKernelRo = AARCH64_MMU_AP_KERNEL_RO;
    inline constexpr U64 ApUserRw = AARCH64_MMU_AP_USER_RW;
    inline constexpr U64 ApUserRo = AARCH64_MMU_AP_USER_RO;
    inline constexpr U64 ShInner = AARCH64_MMU_SH_INNER;
    inline constexpr U64 AccessFlag = AARCH64_MMU_ACCESS_FLAG;
    inline constexpr U64 Pxn = AARCH64_MMU_PXN;
    inline constexpr U64 Uxn = AARCH64_MMU_UXN;
    inline constexpr U64 DeviceFlags = AARCH64_MMU_DEVICE_FLAGS; // Device memory flags
    inline constexpr U64 KernelCodeFlags = AARCH64_MMU_KERNEL_CODE_FLAGS; // Kernel code flags
    inline constexpr U64 MairValue = AARCH64_MMU_MAIR_VALUE; // MAIR value

    inline U64 tcr_value(void) {
        return aarch64_mmu_tcr_value();
    }

    inline U64 level_index(U64 address, U64 shift) {
        return aarch64_mmu_level_index(address, shift);
    }

} // namespace mm::backend

#endif // KERNEL_INCLUDE_MM_ARCH_AARCH64_MM_BACKEND_H