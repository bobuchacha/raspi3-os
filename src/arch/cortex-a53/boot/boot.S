#include "memory.h"
#include "../macros.S"
#include "arch/cortex-a53/sysregs.h"

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
	mov     x0, #(3 << 20)			// We enable FP / SIMD here to make sure that we can
  									// access FPSR register later when saving the PState
	msr     cpacr_el1, x0
	isb
 
	// enable AArch64 in EL1
	mov     x0, #(1 << 31)              // AArch64
	orr     x0, x0, #(1 << 1)           // SWIO hardwired on Pi3
	// and 	x0, x0, #(~HCR_EL2_AMO)	// SError on EL1
	// and 	x0, x0, #(~HCR_EL2_IMO)	// IRQ on EL1
	// and 	x0, x0, #(~HCR_EL2_FMO)	// FIQ on EL1
	// and 	x0, x0, #(~HCR_EL2_E2H)
	// and 	x0, x0, #(~HCR_EL2_TGE)
	msr     hcr_el2, x0

	// Setup SCTLR access
	mov     x2, #0x0800
	movk    x2, #0x30d0, lsl #16
	msr     sctlr_el1, x2

	// set up exception handlers
	// ignore for now. Setup later in kernel_main
	// ldr     x2, =vector_table
	// msr     vbar_el1, x2

	// change execution level to EL1
	mov     x2, #(0x3c4 + 1)		// +1 to use SPSel
	msr     spsr_el2, x2
	adr     x2, el1_entry
	msr     elr_el2, x2

	// clear EL1 system registers
	msr     elr_el1, xzr
	msr     far_el1, xzr

	
	// update el1 sp to use kernel address space before going to EL1
	// mov 	x1, #VA_START	
	// mrs 	x2, sp_el1
	// add 	x2, x2, x1
	// msr 	sp_el1, x2

	eret


el1_entry:
	// now we use SP_EL0
	mov     sp, x1		                // setup our stack. Still stored in x1
	bl		uart1_init
	bl		boot_init_mmu

	// reset stack to low memory
	mov 	x1, #VA_START	
	add		sp, x1, #LOW_MEMORY_CEILING	// sp = VA_START + LOW_MEMORY_CEILING

	bl 		clear_bss					// clear bss

jump_to_kernel_main:
	
    ldr x10, =kernel_main
    br x10

    b proc_hang
/********************************************************** END OF BOOT CODE ***************************************************/

//--------------------------------------------------------------
// func: copy memory
//--------------------------------------------------------------
clear_bss:
	ldr     x1, =bss_begin
	ldr     w2, =bss_size
  3:  
    cbz     w2, 4f	   // if (!w2--) goto setup_page_tables
	str     xzr, [x1], #8
	sub     w2, w2, #1
	cbnz    w2, 3b
  4:
	ret



//========================================== DATA ==========================================
.section ".data.low"
.global test_string
test_string: 				.asciz "\n\r\n\rHello.\n\rHow are you?\n\rI am under the water.\n\rPlease help me.\n\r\n\r"
err_unsupported_granule: 	.asciz "\n[!] Unsupported 4K Granule\n"
msg_init_mmu:				.asciz "\n[i] Initializing MMU\n"

VAR trace_1
	.asciz "Trace 1\n"
VAR trace_2
 	.asciz "Trace 2\n"
VAR trace_3
 	.asciz "Trace 3\n"
VAR trace_4 
	.asciz "Trace 4\n"
VAR trace_5
	.asciz "Trace 5\n"
VAR trace_6 
	.asciz "Trace 6\n"
