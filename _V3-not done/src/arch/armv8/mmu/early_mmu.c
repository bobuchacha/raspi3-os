#include "arch/armv8/defs.h"
#include "arch/armv8/bootcode.h"
#include "mair.h"
#include "pte.h"
#include "tcr.h"

#define GET_PGD_ID(addr) (addr >> 39 & 0x1FF)   // get bit 39-47 of a number
#define GET_PUD_ID(addr) (addr >> 30 & 0x1FF)   // get bit 30-38 of an address
#define GET_PMD_ID(addr) (addr >> 21 & 0x1FF)   // get bit 21-29 of the address
#define GET_PAGE_ID(addr) (addr >> 12 & 0x1FF)  // get bit 12-20 of the address
#define GET_FRAME_OFFSET(addr) (addr & 0xFFF)   // get first 12 bit 0-11
#define TRIM_ADDRESS_TO_LOWER_4K(addr) (addr & 0xFFFFFFFFFFFFF000ull)
#define GET_NEXT_TABLE_ADDRESS(ull) (ull & 0x0000FFFFFFFFF000ull)

#define MAIR_VALUE          0
#define CONFIG_VA_BITS      48          // 48 bit virtual address
#define CONFIG_GRANULE_SIZE 4KB         // 4KB granule

#define pre_granule(s, t) TCR_TT ## t ## _GRANULE_ ## s
#define eval(s, t) pre_granule(s, t)
#define granule(t) eval(CONFIG_GRANULE_SIZE, t)

#define sync_all() __asm volatile("dsb sy")
#define invalidate_tlbs_el(el) \
	__asm volatile( \
		"tlbi vmalle" #el ";" \
		"dsb sy;" \
		"isb")
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

/**
 * look up translate table
 * @param va
 * @return
 */
BOOTFUNC
static ULONG ttbr_lookup(register ULONG va) {

    register ULONG pa;

    __asm volatile(
            "at S1E1R, %0;"
            "mrs %1, par_el1;"
            : "=r" (pa)
            : "r" (va)
            );

    if (pa & 0x1)
        return 0;

    return pa;
}

BOOTFUNC
static void _mme_set_mair(void){
    register long mair = (MAIR_DEVICE << (MAIR_DEVICE_INDEX * 8)) |
                         (MAIR_CACHEABLE << (MAIR_CACHEABLE_INDEX * 8)) |
                         (MAIR_NON_CACHEABLE << (MAIR_NON_CACHEABLE_INDEX * 8));
    __asm volatile(
            "msr mair_el1, %0;"
            : // no output
            : "r" (mair));

}

BOOTFUNC
void _mmu_map_kernel(void) {
    register unsigned long i;

    // map high memory - for c codes
    // point first PGD entry to the base of PUD
    ttbr1_pgd[0] = (unsigned long long)ttbr1_pud | PE_KERNEL_CODE | PT_TABLE_ENTRY;
    // point first PUD entry to our PMD
    ttbr1_pud[0] = (unsigned long long)ttbr1_pmd | PE_KERNEL_CODE | PT_TABLE_ENTRY;

    // map first 16 MB to kernel
    for (i = 0; i < 0x1000000; i += 0x200000) {
        ttbr1_pmd[GET_PMD_ID(i)] = (unsigned long long)i | PE_KERNEL_CODE | PT_BLOCK_ENTRY;
    }

    // identity map normal memory from 16MB - DEVICE_BASE
    // modify this to map another PMD if we have additional RAM
    for (i = 0x1000000; i < DEVICE_BASE; i += 0x200000) {
        ttbr1_pmd[GET_PMD_ID(i)] = (unsigned long long)i | PE_KERNEL_DATA | PT_BLOCK_ENTRY;
    }

    // map low memory - for boot codes
    // point first PGD entry to the base of PUD
    ttbr0_pgd[0] = (unsigned long long)ttbr0_pud | PE_KERNEL_CODE | PT_TABLE_ENTRY;
    // point first PUD entry to our PMD
    // ttbr0_pud[0] = (unsigned long long)ttbr0_pmd | PE_KERNEL_CODE | PT_TABLE_ENTRY;
    ttbr0_pud[0] = (unsigned long long)0 | PE_KERNEL_CODE | PT_BLOCK_ENTRY;             // map first 1GB of memory to the low so kernel gets access to real physical memory
    // point first PMD entry to first 2MB memory
    // ttbr0_pmd[0] = (unsigned long long)0 | PE_KERNEL_CODE | PT_BLOCK_ENTRY;
    return;
}

BOOTFUNC
void _mmu_map_device(void) {
    register unsigned long i;

    for (i=DEVICE_BASE; i < DEVICE_MEMORY_SIZE; i+=0x200000) {
        register unsigned int pmdi = GET_PMD_ID(i);
        ttbr0_pmd[pmdi] = (unsigned long long)i | PE_DEVICE | PT_BLOCK_ENTRY;
        ttbr1_pmd[pmdi] = (unsigned long long)i | PE_DEVICE | PT_BLOCK_ENTRY;
    }

    return;
}

BOOTFUNC
static void _mmu_set_tcr(void) {

    register long tcr = 0;

    /* TTBR1 */
    tcr |= (64 - CONFIG_VA_BITS);
    tcr |= TCR_MISS_FAULT;
    tcr |= (TCR_CACHEABLE_WB_WA << 8);
    tcr |= (TCR_CACHEABLE_WB_WA << 10);
    tcr |= TCR_INNER_SHAREABLE;
    tcr <<= 16; /* it's important to keep these
		         * after the shift! */
    tcr |= (TCR_TOP_BYTE_USED << 1);
    tcr |= granule(1);
    /* for some reason bit 23 is set which disable TTBR1 */
    /* clear it here to enable TTBR1*/
    tcr &= ~(TCR_EPD1_TTBR1_DISABLED);

    /* TTBR0 */
    tcr |= (64 - CONFIG_VA_BITS);
    tcr |= TCR_MISS_NO_FAULT;
    tcr |= (TCR_CACHEABLE_WB_WA << 8);
    tcr |= (TCR_CACHEABLE_WB_WA << 10);
    tcr |= TCR_INNER_SHAREABLE;
    tcr |= granule(0);
    tcr |= TCR_TOP_BYTE_USED;

    /* common */
    tcr |= TCR_IPA_32BIT;
    tcr |= TCR_ASID_TTBR0;
    tcr |= TCR_ASID_8BIT;

    __asm volatile(
            "msr tcr_el1, %0;"
            "isb;"
            : // no output
            : "r" (tcr)
            );

    __asm volatile(
            "mrs %0, tcr_el1; isb"
            : "=r" (tcr) :
            );
}


BOOTFUNC
void boot_init_mmu(){

    load_table("0", ttbr0_pgd);
    load_table("1", ttbr1_pgd);

    _mmu_map_kernel();
    _mmu_map_device();

    _mme_set_mair();
    _mmu_set_tcr();

    // enable MMU
    asm volatile(
            "dsb ish; isb; msr sctlr_el1, %0;"
            "isb; nop; nop; nop; nop"
            :
            :"r"(0x5 | (1 << 12))
            );


}

