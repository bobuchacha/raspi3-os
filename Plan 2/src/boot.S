#include "../../include/arm/sysregs.h"

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
	msr     elr_el1, xzr
	msr     far_el1, xzr

	eret

el1_entry:
	mov     sp, x1		                // setup our stack. Still stored in x1


clear_bss:
	ldr     x1, =bss_begin
	ldr     w2, =bss_size
  3:  
    cbz     w2, _init_mmu	   // if (!w2--) goto setup_page_tables
	str     xzr, [x1], #8
	sub     w2, w2, #1
	cbnz    w2, 3b

    // init MMU
_init_mmu:
    bl      mmu_init

jump_to_kernel_main:
    b kernel_main
    b proc_hang
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

