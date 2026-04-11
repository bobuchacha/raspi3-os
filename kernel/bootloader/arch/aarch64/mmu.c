#include "boot_mmu.h"
#include "types.h"

static U64 g_boot_ttbr0_l0[AARCH64_MMU_TABLE_ENTRIES] PAGE_ALIGNED;
static U64 g_boot_ttbr0_l1[AARCH64_MMU_TABLE_ENTRIES] PAGE_ALIGNED;
static U64 g_boot_ttbr0_l2[AARCH64_MMU_TABLE_ENTRIES] PAGE_ALIGNED;
static U64 g_boot_ttbr1_l0[AARCH64_MMU_TABLE_ENTRIES] PAGE_ALIGNED;
static U64 g_boot_ttbr1_l1[AARCH64_MMU_TABLE_ENTRIES] PAGE_ALIGNED;
static U64 g_boot_ttbr1_l2[AARCH64_MMU_TABLE_ENTRIES] PAGE_ALIGNED;

static PhysAddr table_phys(const U64* table) {
    return (PhysAddr)(Uptr)table;
}

static U64 make_table_descriptor(const U64* table) {
    return table_phys(table) | AARCH64_MMU_DESC_TABLE;
}

static U64 make_block_descriptor(PhysAddr phys, U64 flags) {
    return phys | flags | AARCH64_MMU_DESC_BLOCK;
}

static void map_low_gib(U64* l2_table) {
    U64 index;

    for (index = 0; index < AARCH64_MMU_TABLE_ENTRIES; ++index) {
        const PhysAddr phys = (PhysAddr)(index << AARCH64_MMU_L2_SHIFT);
        const U64 flags = ((phys >= BOOT_MMU_DEVICE_BASE) && (phys < BOOT_MMU_DEVICE_LIMIT))
            ? AARCH64_MMU_DEVICE_FLAGS
            : AARCH64_MMU_KERNEL_CODE_FLAGS;

        l2_table[index] = make_block_descriptor(phys, flags);
    }
}

void boot_enable_mmu(void) {
    U64 sctlr;
    const U64 mair = AARCH64_MMU_MAIR_VALUE;
    const U64 tcr = aarch64_mmu_tcr_value();

    memzero(g_boot_ttbr0_l0, sizeof(g_boot_ttbr0_l0));
    memzero(g_boot_ttbr0_l1, sizeof(g_boot_ttbr0_l1));
    memzero(g_boot_ttbr0_l2, sizeof(g_boot_ttbr0_l2));
    memzero(g_boot_ttbr1_l0, sizeof(g_boot_ttbr1_l0));
    memzero(g_boot_ttbr1_l1, sizeof(g_boot_ttbr1_l1));
    memzero(g_boot_ttbr1_l2, sizeof(g_boot_ttbr1_l2));

    g_boot_ttbr0_l0[0] = make_table_descriptor(g_boot_ttbr0_l1);
    g_boot_ttbr0_l1[0] = make_table_descriptor(g_boot_ttbr0_l2);
    map_low_gib(g_boot_ttbr0_l2);

#if BOOT_MMU_EXTRA_RAM_BASE != 0ULL
    g_boot_ttbr0_l1[1] = make_block_descriptor(BOOT_MMU_EXTRA_RAM_BASE, AARCH64_MMU_KERNEL_CODE_FLAGS);
#endif

    g_boot_ttbr1_l0[0] = make_table_descriptor(g_boot_ttbr1_l1);
    g_boot_ttbr1_l1[0] = make_table_descriptor(g_boot_ttbr1_l2);
    map_low_gib(g_boot_ttbr1_l2);

#if BOOT_MMU_EXTRA_RAM_BASE != 0ULL
    g_boot_ttbr1_l1[1] = make_block_descriptor(BOOT_MMU_EXTRA_RAM_BASE, AARCH64_MMU_KERNEL_CODE_FLAGS);
#endif

    __asm__ volatile("msr ttbr0_el1, %0\n"
        "msr ttbr1_el1, %1\n"
        "msr mair_el1, %2\n"
        "msr tcr_el1, %3\n"
        "isb\n"
        :
    : "r"(table_phys(g_boot_ttbr0_l0)),
        "r"(table_phys(g_boot_ttbr1_l0)),
        "r"(mair),
        "r"(tcr)
        : "memory");

    __asm__ volatile("tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory");

    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
    __asm__ volatile("msr sctlr_el1, %0\n"
        "isb\n"
        :
    : "r"(sctlr)
        : "memory");
}