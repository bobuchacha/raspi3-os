#include "arm/sysregs.h"
#include "memory.h"

#include "../include/asm_macros.inc"

//---------------------------------------------------------------------------------------------------------------------
// MAIN CODE HERE
//---------------------------------------------------------------------------------------------------------------------
.section ".text.boot"

.globl _start
_start:
    b entrypoint

signature:      padded_string "ROS.KRL\0", 8                                                // 8 byte signature
executable:     .word 0                                                                     // 4 bytes dummy flag
author:         padded_string "THANG CAO", 32
copyright:      padded_string "Copyright (c) 2024 Renaissance Pi OS", 64                    // 64 bytes
version:        padded_string "0.0.1", 32                                                   // 32 bytes

entrypoint:
	mrs	x1, mpidr_el1
	and	x1, x1, #0xFF		// Check processor id (0xFF)
	cbz	x1, master		// Hang for all non-primary CPU
	b	proc_hang

    // cpu id > 0, stop
proc_hang: 
	b 	proc_hang

    // cpu id == 0
master:

    // set top of stack just before our code (stack grows to a lower address per AAPCS64)
    ldr     x1, =_start

    // set up EL1
    mrs     x0, CurrentEL
    and     x0, x0, #12 // clear reserved bits

    // running at EL3?
    cmp     x0, #12
    bne     switch_to_EL1

    // should never be executed, just for completeness
    mov     x2, #0x5b1
    msr     scr_el3, x2
    mov     x2, #0x3c9
    msr     spsr_el3, x2
    adr     x2, _switch_to_EL1_continue
    msr     elr_el3, x2
    eret    // jump to EL1


switch_to_EL1:
    cmp     x0, #4
    beq     _switch_to_EL1_continue
    msr     sp_el1, x1

    // enable CNTP for EL1
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    // disable coprocessor traps
    mov     x0, #0x33FF
    msr     cptr_el2, x0
    msr     hstr_el2, xzr
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0

    // enable AArch64 in EL1
    mov     x0, #(1 << 31)      // AArch64
    orr     x0, x0, #(1 << 1)   // SWIO hardwired on Pi3
    msr     hcr_el2, x0
    mrs     x0, hcr_el2

    // Setup SCTLR access
    mov     x2, #0x0800
    movk    x2, #0x30d0, lsl #16
    msr     sctlr_el1, x2

    // set up exception handlers - moved to irq.S
    // I need to set this first, so after the MMU is enable the CPU will jump to fork_exit() and call schedule_tail, it then call kernel_main
    ldr     x2, = vectors_debugger      // for debugger
    msr     vbar_el1, x2

    // change execution level to EL1
    mov     x2, #0x3c4
    msr     spsr_el2, x2
    adr     x2, _switch_to_EL1_continue
    msr     elr_el2, x2

    // clear EL1 system registers
    msr     elr_el1, xzr
    msr     far_el1, xzr

    eret    // jump to EL1

_switch_to_EL1_continue:
    mov     sp, x1      // set stack to _start

el1_entry:
	adr	x0, bss_begin
	adr	x1, bss_end
	sub	x1, x1, x0
	bl 	memzero             // zero out BSS


init_mmu:

    // prepare virtual memory for main kernel.
    // we are going to fill in the empty directories
    bl 	__create_page_tables
_after_pg_tlb:
    // update stack pointer to use our virtual memory. Since
    // kernel now use kernel address space, starting at VA_START, we just need to add into our stack pointer
    // and make sure stack is right below LOW_MEMORY which is 4MB mark

    mov	x0, #VA_START
    add	sp, x0, #LOW_MEMORY     // for some reason sp is still at physical LOW_MEMORY

    ldr	x0, =(TCR_VALUE)        // load Translation Control Register
    msr	tcr_el1, x0

    isb // disable interrupt

    adrp	x0, pg_dir          // load our kernel selector to base of kernel directory
    msr	ttbr1_el1, x0


    ldr	x0, =(MAIR_VALUE)       // Load our MAIR value
    msr	mair_el1, x0

    // we need to load kernel entry point into x2 now. This is the absolute address of kernel_main
    // we must do this before enabling MMU
    ldr	x2, =kernel_main

//    dsb ish
    //isb

    _crashed_point:
//    ldr	x0, =(SCTLR_VALUE_MMU_ENABLED)  // enable MMU
    mov x0, #1
    msr	sctlr_el1, x0

    // CODE WILL NEVER REACH HERE FOR SOME REASON. IT WILL JUMP TO ret_from_fork()
    // BY SOME KIND OF INTERRUPTS THEN CONTINUE
    isb

    br x2

//1:
//    b 1b
//    br x2                     // jump to kernel main
//
//jump_to_kernel:
//    b kernel_main
1:
    b 1b

__create_page_tables:
    mov	x29, x30						// save return address

    adrp	x0, pg_dir          // zero out our directories
    mov	x1, #PG_DIR_SIZE        // it is 3 pages total in length
    bl 	memzero

    adrp	x0, pg_dir          // map our kernel address space
    mov	x1, #VA_START
    create_pgd_entry x0, x1, x6, x7 // table holds in x0, virtual address in x1, x2 and x3 are registers for temporary calculating

    /* Mapping kernel and init stack. */
    mov 	x1, xzr							// start mapping from physical offset 0
    mov 	x2, #VA_START						// first virtual address
    ldr	x3, =(VA_START + DEVICE_BASE - SECTION_SIZE)		// last virtual address
    create_block_map x0, x1, x2, x3, MMU_FLAGS, x4      // block type, normal access, no cache

    /* Mapping device memory*/
    mov 	x1, #DEVICE_BASE					// start mapping from device base address
    ldr 	x2, =(VA_START + DEVICE_BASE)		// first virtual address. TODO: change this to our peripheral address start
    ldr	x3, =(VA_START + PHYS_MEMORY_SIZE - SECTION_SIZE)	// last virtual address
    create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4

    mov x30, x29    // restore return address
    ret
