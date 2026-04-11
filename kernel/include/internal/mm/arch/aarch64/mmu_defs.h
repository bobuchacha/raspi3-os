#ifndef KERNEL_INTERNAL_MM_ARCH_AARCH64_MMU_DEFS_H
#define KERNEL_INTERNAL_MM_ARCH_AARCH64_MMU_DEFS_H

#include "types.h"

/*
 * MMCU Configurations
 */

#define AARCH64_MMU_TABLE_ENTRIES 512ULL        // Number of entries in a translation table
#define AARCH64_MMU_PAGE_SHIFT 12ULL            // Page size shift (4KB pages)
#define AARCH64_MMU_L0_SHIFT 39ULL              // Level 0 table shift
#define AARCH64_MMU_L1_SHIFT 30ULL              // Level 1 table shift
#define AARCH64_MMU_L2_SHIFT 21ULL              // Level 2 table shift. Why 21? 
#define AARCH64_MMU_INDEX_MASK 0x1FFULL         // Index mask. Why 0x1FF?
#define AARCH64_MMU_L1_BLOCK_SIZE (1ULL << AARCH64_MMU_L1_SHIFT)
#define AARCH64_MMU_L2_BLOCK_SIZE (1ULL << AARCH64_MMU_L2_SHIFT)
#define AARCH64_MMU_OUTPUT_ADDRESS_MASK 0x0000FFFFFFFFF000ULL

#define AARCH64_MMU_DESC_INVALID 0ULL       // Invalid descriptor
#define AARCH64_MMU_DESC_BLOCK 0x1ULL       // Block descriptor
#define AARCH64_MMU_DESC_TABLE 0x3ULL       // Table descriptor

#define AARCH64_MMU_ATTR_DEVICE 0ULL        // Device memory
#define AARCH64_MMU_ATTR_NORMAL 1ULL        // Normal memory
#define AARCH64_MMU_ATTR_NON_CACHEABLE 2ULL // Non-cacheable memory

#define AARCH64_MMU_AP_KERNEL_RW (0ULL << 6) // Kernel read/write
#define AARCH64_MMU_AP_KERNEL_RO (2ULL << 6) // Kernel read-only
#define AARCH64_MMU_AP_USER_RW (1ULL << 6)   // User read/write
#define AARCH64_MMU_AP_USER_RO (3ULL << 6)   // User read-only

#define AARCH64_MMU_SH_OUTER (2ULL << 8)    // Outer shareable
#define AARCH64_MMU_SH_INNER (3ULL << 8)    // Inner shareable
#define AARCH64_MMU_ACCESS_FLAG (1ULL << 10) // Access flag
#define AARCH64_MMU_PXN (1ULL << 53)        // Privileged execute-never
#define AARCH64_MMU_UXN (1ULL << 54)        // User execute-never

 // Device memory flags. Device memory is non-cacheable and strongly ordered
#define AARCH64_MMU_DEVICE_FLAGS \
    (((AARCH64_MMU_ATTR_DEVICE << 2) | AARCH64_MMU_AP_KERNEL_RW | \
      AARCH64_MMU_PXN | AARCH64_MMU_UXN | AARCH64_MMU_SH_OUTER) | \
     AARCH64_MMU_ACCESS_FLAG)

// Kernel data flags. 
#define AARCH64_MMU_KERNEL_DATA_FLAGS \
    (((AARCH64_MMU_ATTR_NON_CACHEABLE << 2) | AARCH64_MMU_AP_KERNEL_RW | \
      AARCH64_MMU_PXN | AARCH64_MMU_UXN | AARCH64_MMU_SH_INNER) | \
     AARCH64_MMU_ACCESS_FLAG)

// Kernel read-only flags. 
#define AARCH64_MMU_KERNEL_RO_FLAGS \
    (((AARCH64_MMU_ATTR_NORMAL << 2) | AARCH64_MMU_AP_KERNEL_RO | \
      AARCH64_MMU_PXN | AARCH64_MMU_UXN | AARCH64_MMU_SH_INNER) | \
     AARCH64_MMU_ACCESS_FLAG)

// Kernel code flags. 
#define AARCH64_MMU_KERNEL_CODE_FLAGS \
    (((AARCH64_MMU_ATTR_NORMAL << 2) | AARCH64_MMU_AP_KERNEL_RW | \
      AARCH64_MMU_UXN | AARCH64_MMU_SH_INNER) | \
     AARCH64_MMU_ACCESS_FLAG)

// User data flags. 
#define AARCH64_MMU_USER_DATA_FLAGS \
    (((AARCH64_MMU_ATTR_NON_CACHEABLE << 2) | AARCH64_MMU_AP_USER_RW | \
      AARCH64_MMU_PXN | AARCH64_MMU_UXN | AARCH64_MMU_SH_INNER) | \
     AARCH64_MMU_ACCESS_FLAG)

#define AARCH64_MMU_USER_RO_FLAGS \
    (((AARCH64_MMU_ATTR_NORMAL << 2) | AARCH64_MMU_AP_USER_RO | \
      AARCH64_MMU_PXN | AARCH64_MMU_UXN | AARCH64_MMU_SH_INNER) | \
     AARCH64_MMU_ACCESS_FLAG)

#define AARCH64_MMU_USER_CODE_FLAGS \
    (((AARCH64_MMU_ATTR_NORMAL << 2) | AARCH64_MMU_AP_USER_RO | \
      AARCH64_MMU_PXN | AARCH64_MMU_SH_INNER) | \
     AARCH64_MMU_ACCESS_FLAG)

#define AARCH64_MMU_MAIR_DEVICE 0x04ULL         // Device memory
#define AARCH64_MMU_MAIR_CACHEABLE 0xFFULL      // Cacheable memory
#define AARCH64_MMU_MAIR_NON_CACHEABLE 0x44ULL  // Non-cacheable memory
#define AARCH64_MMU_MAIR_VALUE \
    ((AARCH64_MMU_MAIR_DEVICE << 0) | \
     (AARCH64_MMU_MAIR_CACHEABLE << 8) | \
     (AARCH64_MMU_MAIR_NON_CACHEABLE << 16))

#define AARCH64_MMU_TCR_T0SZ 16ULL
#define AARCH64_MMU_TCR_T1SZ (16ULL << 16)
#define AARCH64_MMU_TCR_IRGN_WB_WA (1ULL << 8)
#define AARCH64_MMU_TCR_ORGN_WB_WA (1ULL << 10)
#define AARCH64_MMU_TCR_SH_INNER (3ULL << 12)
#define AARCH64_MMU_TCR_TG0_4KB (0ULL << 14)
#define AARCH64_MMU_TCR_TG1_4KB (2ULL << 30)

static inline U64 aarch64_mmu_tcr_value(void) {
    return AARCH64_MMU_TCR_T0SZ |
        AARCH64_MMU_TCR_T1SZ |
        AARCH64_MMU_TCR_IRGN_WB_WA |
        AARCH64_MMU_TCR_ORGN_WB_WA |
        AARCH64_MMU_TCR_SH_INNER |
        AARCH64_MMU_TCR_TG0_4KB |
        AARCH64_MMU_TCR_TG1_4KB |
        (AARCH64_MMU_TCR_IRGN_WB_WA << 16) |
        (AARCH64_MMU_TCR_ORGN_WB_WA << 16) |
        (AARCH64_MMU_TCR_SH_INNER << 16);
}

static inline U64 aarch64_mmu_level_index(U64 address, U64 shift) {
    return (address >> shift) & AARCH64_MMU_INDEX_MASK;
}

#endif // KERNEL_INTERNAL_MM_ARCH_AARCH64_MMU_DEFS_H