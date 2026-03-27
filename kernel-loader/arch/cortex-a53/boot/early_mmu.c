#include "ros.h"
#include "arch/cortex-a53/mmu.h"
#include "arch/cortex-a53/boot/bootcode.h"
#include "arch/cortex-a53/boot/uart1.h"
#include "device/raspi3b.h"

#define GET_PGD_ID(addr) (addr >> 39 & 0x1FF)
#define GET_PUD_ID(addr) (addr >> 30 & 0x1FF)
#define GET_PMD_ID(addr) (addr >> 21 & 0x1FF)

#define CONFIG_VA_BITS      48
#define CONFIG_GRANULE_SIZE 4KB

#define pre_granule(s, t) TCR_TT ## t ## _GRANULE_ ## s
#define eval(s, t) pre_granule(s, t)
#define granule(t) eval(CONFIG_GRANULE_SIZE, t)

#define load_table(table, table_base) \
	__asm volatile( \
		"msr ttbr" table "_el1, %0;" \
		:: "r" (table_base) \
	)

extern unsigned long ttbr0_pgd[];
extern unsigned long ttbr0_pud[];
extern unsigned long ttbr0_pmd[];

extern unsigned long ttbr1_pgd[];
extern unsigned long ttbr1_pud[];
extern unsigned long ttbr1_pmd[];

BOOTFUNC
static void _mme_set_mair(void) {
    register long mair = (MAIR_DEVICE << (MAIR_DEVICE_INDEX * 8)) |
        (MAIR_CACHEABLE << (MAIR_CACHEABLE_INDEX * 8)) |
        (MAIR_NON_CACHEABLE << (MAIR_NON_CACHEABLE_INDEX * 8));
    __asm volatile(
    "msr mair_el1, %0;"
        :
    : "r" (mair));
}

BOOTFUNC
void _mmu_map_kernel(void) {
    register unsigned long i;

    ttbr1_pgd[0] = (unsigned long long)ttbr1_pud | PE_KERNEL_CODE | PT_TABLE_ENTRY;
    ttbr1_pud[0] = (unsigned long long)ttbr1_pmd | PE_KERNEL_CODE | PT_TABLE_ENTRY;

    for (i = 0; i < 0x1000000; i += 0x200000) {
        ttbr1_pmd[GET_PMD_ID(i)] = (unsigned long long)i | PE_KERNEL_CODE | PT_BLOCK_ENTRY;
    }

    for (i = 0x1000000; i < DEVICE_BASE; i += 0x200000) {
        ttbr1_pmd[GET_PMD_ID(i)] = (unsigned long long)i | PE_KERNEL_CODE | PT_BLOCK_ENTRY;
    }

    ttbr0_pgd[0] = (unsigned long long)ttbr0_pud | PE_KERNEL_CODE | PT_TABLE_ENTRY;
    ttbr0_pud[0] = (unsigned long long)0 | PE_KERNEL_CODE | PT_BLOCK_ENTRY;
}

BOOTFUNC
void _mmu_map_device(void) {
    register unsigned long i;

    for (i = DEVICE_BASE; i < DEVICE_MEMORY_SIZE; i += 0x200000) {
        register unsigned int pmdi = GET_PMD_ID(i);
        ttbr0_pmd[pmdi] = (unsigned long long)i | PE_DEVICE | PT_BLOCK_ENTRY;
        ttbr1_pmd[pmdi] = (unsigned long long)i | PE_DEVICE | PT_BLOCK_ENTRY;
    }
}

BOOTFUNC
static void _mmu_set_tcr(void) {
    register long tcr = 0;

    tcr |= (64 - CONFIG_VA_BITS);
    tcr |= TCR_MISS_FAULT;
    tcr |= (TCR_CACHEABLE_WB_WA << 8);
    tcr |= (TCR_CACHEABLE_WB_WA << 10);
    tcr |= TCR_INNER_SHAREABLE;
    tcr <<= 16;
    tcr |= (TCR_TOP_BYTE_USED << 1);
    tcr |= granule(1);
    tcr &= ~(TCR_EPD1_TTBR1_DISABLED);

    tcr |= (64 - CONFIG_VA_BITS);
    tcr |= TCR_MISS_NO_FAULT;
    tcr |= (TCR_CACHEABLE_WB_WA << 8);
    tcr |= (TCR_CACHEABLE_WB_WA << 10);
    tcr |= TCR_INNER_SHAREABLE;
    tcr |= granule(0);
    tcr |= TCR_TOP_BYTE_USED;
    tcr |= TCR_IPA_32BIT;
    tcr |= TCR_ASID_TTBR0;
    tcr |= TCR_ASID_8BIT;

    __asm volatile(
    "msr tcr_el1, %0;"
        "isb;"
        :
    : "r" (tcr));

    __asm volatile(
    "mrs %0, tcr_el1; isb"
        : "=r" (tcr) :
        );
}

BOOTFUNC
void boot_init_mmu() {
    load_table("0", ttbr0_pgd);
    load_table("1", ttbr1_pgd);

    _mmu_map_kernel();
    _mmu_map_device();
    _mme_set_mair();
    _mmu_set_tcr();

    asm volatile(
        "dsb ish; isb; msr sctlr_el1, %0;"
        "isb; nop; nop; nop; nop"
        :
    : "r"(0x5 | (1 << 12))
        );
}
