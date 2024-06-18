#define VA_START 		0xffff000000000000      // We will use kernel address space. This is to select TLBR1
#define PHYS_MEMORY_SIZE 		0x40000000      // 1Gb memory Raspberry Pi 3

#define PAGE_MASK			0xfffffffffffff000  // mask the address to get page base address
#define PAGE_SHIFT	 		12                  // number of bits to index a page content
#define TABLE_SHIFT 			9               // number of bits to index a table content
#define SECTION_SHIFT			(PAGE_SHIFT + TABLE_SHIFT)  // 2M indexing

#define PAGE_SIZE   			(1 << PAGE_SHIFT)   // the page size in byte. Should be 4K
#define SECTION_SIZE			(1 << SECTION_SHIFT)    // section size in byte. Should be 2M

#define LOW_MEMORY              	(2 * SECTION_SIZE)  // first 4M will be low memory
#define HIGH_MEMORY             	DEVICE_BASE         // peripheral base start. Device MMIO takes about 15MB physical address space

#define PAGING_MEMORY 			(HIGH_MEMORY - LOW_MEMORY)  // our page start from 4M to 1008 MB
#define PAGING_PAGES 			(PAGING_MEMORY/PAGE_SIZE)

#define ENTRY_PER_TABLE			(1 << TABLE_SHIFT)

#define PGD_SHIFT			(PAGE_SHIFT + 3*TABLE_SHIFT)
#define PUD_SHIFT			(PAGE_SHIFT + 2*TABLE_SHIFT)
#define PMD_SHIFT			(PAGE_SHIFT + TABLE_SHIFT)

#define PG_DIR_SIZE			(3 * PAGE_SIZE)     // initially we have 3 table
#define DEVICE_BASE     0x3F000000      // our peripherals MMIO base

#define TCR_1 (0b00LL << 37) // TBI=0, no tagging
//    (b << 32) |      // IPS=autodetected
#define TCR_2    (0b10LL << 30) // TG1=4k
#define TCR_3 (0b11LL << 28) // SH1=3 inner
#define TCR_4 (0b01LL << 26)  // ORGN1=1 write back
#define TCR_5 (0b01LL << 24)  // IRGN1=1 write back
#define TCR_6 (0b0LL  << 23)  // EPD1 enable higher half
#define TCR_7 (25LL   << 16)  // T1SZ=25, 3 levels (512G)
#define TCR_8 (0b00LL << 14)  // TG0=4k
#define TCR_9 (0b11LL << 12)  // SH0=3 inner
#define TCR_10 (0b01LL << 10)  // ORGN0=1 write back
#define TCR_11 (0b01LL << 8)   // IRGN0=1 write back
#define TCR_12 (0b0LL  << 7)   // EPD0 enable lower half
#define TCR_13 (25LL   << 0)   // T0SZ=25, 3 levels (512G)

#define MY_TCR_VALUE (TCR_1 | TCR_2 | TCR_3 | TCR_4 | TCR_5 | TCR_6 | TCR_7 | TCR_8 | TCR_9 | TCR_10 | TCR_11 | TCR_12 | TCR_13)


#define SCTLR_2 (1<<24)    //  E0E
#define SCTLR_3 (1<<19)    //  WXN
#define SCTLR_4 (1<<12)    //  I, no instruction cache
#define SCTLR_5 (1<<4)     //  SA0
#define SCTLR_6 (1<<3)     //  SA
#define SCTLR_7 (1<<2)     //  C, no cache at all
#define SCTLR_8 (1<<1)    //  A, no aligment check
#define SCTLR_1 (1<<25)   // EE, little endian translation tables
#define SCTLR_CLR_BITS (SCTLR_1 | SCTLR_2 | SCTLR_3 | SCTLR_4 | SCTLR_5 | SCTLR_6 | SCTLR_7 | SCTLR_8)

//-----------------------------------
// MACRO implement push and pop sp
//-----------------------------------
.macro PUSH, reg
    str   \reg, [sp, #-16]!         // push {register}
.endm
.macro POP, reg
    ldr   \reg, [sp], #16           // pop {register}
.endm



#include "../../../../include/arm/mmu.h"
#include "../../../../include/arm/sysregs.h"
//#include "../../include/memory.h"




.section ".text.boot"

.globl _start
_start:
	mrs	    x0, mpidr_el1
	and	    x0, x0,#0xFF		        // Check processor id
    cbz	    x0, master		            // Hang for all non-primary CPU

proc_hang:
    wfe                                 // wait for wake up event
	b       proc_hang

master: 
    // initialize kernel, system registers, setup stack
    // we want stack to grow from our kernel entry point downward
    ldr     x1, =_start

	// set up EL1
	mrs     x0, CurrentEL	            // get current EL
	and     x0, x0, #12 	            // clear reserved bits

	// running at EL3?
	cmp     x0, #12
	bne     el2_entry

el3_entry:
	// should never be executed, just for completeness
	mov     x2, #0x5b1
	msr     scr_el3, x2
	mov     x2, #0x3c9
	msr     spsr_el3, x2
	adr     x2, el2_entry
	msr     elr_el3, x2
	eret

el2_entry:
	cmp     x0, #4
    beq     el1_entry
    msr     sp_el1, x1		            // set EL1 stack

	// enable CNTP for EL1
	mrs     x0, cnthctl_el2
	orr     x0, x0, #3		            // enable bits [0:1]
	msr     cnthctl_el2, x0
	msr     cntvoff_el2, xzr

	// disable coprocessor traps
	mov     x0, #0x33FF
	msr     cptr_el2, x0
	msr     hstr_el2, xzr
	mov     x0, #(3 << 20)
	msr     cpacr_el1, x0

	// enable AArch64 in EL1
	mov     x0, #(1 << 31)              // AArch64
	orr     x0, x0, #(1 << 1)           // SWIO hardwired on Pi3
	msr     hcr_el2, x0

	// Setup SCTLR access
	mov     x2, #0x0800
	movk    x2, #0x30d0, lsl #16
	msr     sctlr_el1, x2

	// set up exception handlers
	// ignore for now
	// ldr     x2, =_vectors
	// msr     vbar_el1, x2

	// change execution level to EL1
	mov     x2, #0x3c4
	msr     spsr_el2, x2
	adr     x2, el1_entry
	msr     elr_el2, x2

	// clear EL1 system registers
	//msr     elr_el1, xzr
	//msr     far_el1, xzr

	eret

el1_entry:
	mov     sp, x1		                // setup our stack. Still stored in x1

	// clear bss
	ldr     x1, =bss_begin
	ldr     w2, =bss_size
  3:  
    cbz     w2, init_mmu	            // if (!w2--) goto setup_page_tables
	str     xzr, [x1], #8
	sub     w2, w2, #1
	cbnz    w2, 3b

init_mmu:
    bl      setup_page_tables

    // save kernel main address
    mov	    x2, #VA_START
    add	    sp, x2, #LOW_MEMORY

    // check for 4k granule and at least 36 bits physical address bus */
    mrs     x10,  id_aa64mmfr0_el1
    ands    x11, x10, 0xF               // x11 = x10 & 0xF  (our b is here)
    ands    x12, x10, 0xF << 28         // x12 = x10 & 0xF << 28
    cbnz    x12, proc_hang              // if x12 != 0, park CPU - 4k granule or 36 bit address space not supported
    cbz     x11, proc_hang              // if x11 == 0, park CPU - 4k granule or 36 bit address space not supported

    // first, set Memory Attributes array. defined in include/arm/mmu.h
    ldr     x10, =MAIR_VALUE
    msr     mair_el1, x10

    // next, specify mapping characteristics in translate control register
    ldr     x10, =MY_TCR_VALUE
    LSL     x11, x11, #32
    ORR     x10, x10, x11               // x10 = x10 | x11 << 32
    msr     tcr_el1, x10
    isb                                 // force these changes to be seen before enabling MMU

enable_mmu:

    // point kernel to the map
    adrp     x10, pg_dir
    add     x10, x10, #1
    msr     ttbr1_el1, x10
    ldr     x10, =pg_dir
    msr     ttbr0_el1, x10              // now kernel high will be the same as kernel low
    dsb     ish
    isb

    mrs     x10, sctlr_el1
    ORR     x10, x10, #1                // enable MMU bit
___breakpoint:
    ldr	    x15, =kernel_main            // before enabling MMU

    // setup exception handler to jump to kernel_main after MMU is enabled
    ldr     x2, =early_exception_vectors
	msr     vbar_el1, x2
    mov     x10, #1
    msr     sctlr_el1, x10              // <--- ERROR: it stucks here, call some error, vectors, etc
    isb                                 // force the changes to be seen by the next instruction [ATENTION] Exception raises here
    
    // at this point, CPU will create a Prefetch Abort exception because it can not fetch the next
    // instruction (in PC) after MMU is enabled. So we are going to jump to kernel_main from
    // an exception

    //br      x2                          // jump to kernel_main
    //b       proc_hang
/********************************************************** END OF BOOT CODE ***************************************************/



//-----------------------------------
// MACRO create a padded string
//-----------------------------------
.macro padded_string string, max
    1:
        .ascii "\string"
    2:
        .iflt \max - (2b - 1b)
        .error "String too long"
        .endif

        .ifgt \max - (2b - 1b)
        .zero \max - (2b - 1b)
        .endif
.endm


//-----------------------------------
// MACRO create PGD entry
//-----------------------------------
.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
        create_table_entry \tbl, \virt, PGD_SHIFT, \tmp1, \tmp2
        create_table_entry \tbl, \virt, PUD_SHIFT, \tmp1, \tmp2
.endm

#define MM_TYPE_PAGE_TABLE2 (0b11 | (1 << 10) | (1<<6) | (3<<8) | (MT_NORMAL<<2))

//-----------------------------------
// MACRO create a table entry
//-----------------------------------
.macro	create_table_entry, tbl, virt, shift, tmp1, tmp2
        lsr	\tmp1, \virt, #\shift                   // tmp1 = virtual_address >> shift
        and	\tmp1, \tmp1, #ENTRY_PER_TABLE - 1		// table index 
        add	\tmp2, \tbl, #PAGE_SIZE
        orr	\tmp2, \tmp2, #MM_TYPE_PAGE_TABLE       // NORMAL MEMORY
        str	\tmp2, [\tbl, \tmp1, lsl #3]
        add	\tbl, \tbl, #PAGE_SIZE					// next level table page
.endm

//-----------------------------------
// MACRO create a block map
//-----------------------------------
.macro	create_block_map, tbl, phys, start, end, flags, tmp1
        lsr	\start, \start, #SECTION_SHIFT
        and	\start, \start, #ENTRY_PER_TABLE - 1			// table index
        lsr	\end, \end, #SECTION_SHIFT
        and	\end, \end, #ENTRY_PER_TABLE - 1				// table end index
        lsr	\phys, \phys, #SECTION_SHIFT
        mov	\tmp1, #\flags
        orr	\phys, \tmp1, \phys, lsl #SECTION_SHIFT			// table entry
    9999:
        str	\phys, [\tbl, \start, lsl #3]				    // store the entry
        add	\start, \start, #1					            // next entry
        add	\phys, \phys, #SECTION_SIZE				        // next block
        cmp	\start, \end
        b.ls	9999b
.endm



//--------------------------------------------------------------
// func: setup page table
//--------------------------------------------------------------
setup_page_tables:
    mov	    x29, x30					// save return address

    adrp	x0, pg_dir                  // zero out our directories
    mov	    x1, #PG_DIR_SIZE            // it is 3 pages total in length
    bl 	    memzero

    adrp	x0, pg_dir                  // map our kernel address space
    mov	    x1, #VA_START
    create_pgd_entry x0, x1, x6, x7     // table holds in x0, virtual address in x1, x2 and x3 are registers for temporary calculating

    /* Mapping kernel and init stack. */
    mov 	x1, xzr						// start mapping from physical offset 0
    mov 	x2, #VA_START				// first virtual address
    ldr	    x3, =(VA_START + DEVICE_BASE - SECTION_SIZE)		// last virtual address
    create_block_map x0, x1, x2, x3, MMU_FLAGS, x4              // block type, normal access, no cache

    /* Mapping device memory*/
    mov 	x1, #DEVICE_BASE					                // start mapping from device base address
    ldr 	x2, =(VA_START + DEVICE_BASE)		                // first virtual address. TODO: change this to our peripheral address start
    ldr	    x3, =(VA_START + PHYS_MEMORY_SIZE - SECTION_SIZE)	// last virtual address
    create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4

    mov     x30, x29                    // restore return address
    ret

//--------------------------------------------------------------
// func: clear memory
//--------------------------------------------------------------
.globl memzero
memzero:
    str     xzr, [x0], #8
    subs    x1, x1, #8
    b.gt    memzero
    ret

//--------------------------------------------------------------
// func: copy memory
//--------------------------------------------------------------
.globl memcpy
memcpy:
    ldr     x3, [x1], #8
    str     x3, [x0], #8
    subs    x2, x2, #8
    b.gt    memcpy
    ret


//--------------------------------------------------------------
// MACRO: ventry - create a vector entry that jumps to \label
// this will let us successfully jump to kernel start
//--------------------------------------------------------------
.macro	ventry	label
	.align	7
//    mov	    x2, #VA_START
//    add	    sp, x2, #LOW_MEMORY
//    sub     sp, sp, 16
//    POP     x15
    br      x15
.endm


//--------------------------------------------------------------
// an early exception vector that catches Prefetch Abort
//--------------------------------------------------------------
early_exception_vectors:
    ventry  do_jump_to_kernel_main
    ventry  do_jump_to_kernel_main
    ventry  do_jump_to_kernel_main
    ventry  do_jump_to_kernel_main
    ventry  do_jump_to_kernel_main
    ventry  do_jump_to_kernel_main
    ventry  do_jump_to_kernel_main
