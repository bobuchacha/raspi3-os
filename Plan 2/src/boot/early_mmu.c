#include "../../include/ros.h"
#include "../../include/mmu/mair.h"
#include "../../include/mmu/tcr.h"
#include "../../include/mmu/pte.h"
#include "../../include/boot/bootcode.h"
#include "../../include/boot/uart1.h"


#define MAIR_VALUE          0
#define CONFIG_VA_BITS      48          // 48 bit virtual address
#define CONFIG_GRANULE_SIZE 4KB         // 4KB granule

#define pre_granule(s, t) TCR_TT ## t ## _GRANULE_ ## s
#define eval(s, t) pre_granule(s, t)
#define granule(t) eval(CONFIG_GRANULE_SIZE, t)

#define entry_no_t uint16_t
#define entry_t uint64_t

#define sync_all() __asm volatile("dsb sy")
#define invalidate_tlbs_el(el) \
	__asm volatile( \
		"tlbi vmalle" #el ";" \
		"dsb sy;" \
		"isb")



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
static void mmu_set_mair(void){
    register long mair = (MAIR_DEVICE << (MAIR_DEVICE_INDEX * 8)) |
                         (MAIR_CACHEABLE << (MAIR_CACHEABLE_INDEX * 8)) |
                         (MAIR_NON_CACHEABLE << (MAIR_NON_CACHEABLE_INDEX * 8));
    __asm volatile(
            "msr mair_el1, %0;"
            : // no output
            : "r" (mair));

}

BOOTFUNC
void map_kernel(void) {
    register unsigned long i;

    for_each_pte_in(&kpt, pte) {

        curr_addr = 0x0//((pentry_t)&_start)
                    + (pentry_t)(kpt.entry_span * (i++));

        if (curr_addr >= 0x3f000000) {
            break;
        }
        else {
            *pte = curr_addr | PE_KERNEL_CODE | PT_BLOCK_ENTRY;
        }
    }

    dump_table(&kpt);

    return;
}


BOOTFUNC
static void mmu_set_tcr(void) {

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

//    mmu_trace(, "Setting TCR: ", LOG_INFO);
//    _mmu_trace(64, tcr);
//    _mmu_trace(ln, "");

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

//    mmu_trace(, "Set TCR: ", LOG_INFO);
//    _mmu_trace(64, tcr);
//    _mmu_trace(ln, "");

}


BOOTFUNC
void boot_init_mmu(){
    extern unsigned long ttbr0_pgd[];
    extern unsigned long ttbr0_pud[];
    extern unsigned long ttbr0_pmd[];
    extern char *test_string;
    mmu_set_mair();
    asm volatile("breakpoint:");
    mmu_set_tcr();

}

