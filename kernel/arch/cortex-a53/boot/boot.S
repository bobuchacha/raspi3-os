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

.equ SECONDARY_BOOT_STACK_SIZE, 4096

.globl _start
_start:
	mrs	    x0, mpidr_el1
	and	    x0, x0,#0xFF		        // Check processor id
    cbz	    x0, master		            // Hang for all non-primary CPU

proc_hang:
	// Check a per-CPU spin-table for a startup entry address.
	// If the entry is non-zero the secondary should jump to it.
	mrs     x1, mpidr_el1               // read processor id register
	and     x1, x1, #0xFF               // keep lowest byte as CPU index
	ldr     x2, =secondary_entry_table  // address of spin-table (array of pointers)
	lsl     x1, x1, #3                   // multiply index by 8 (sizeof pointer)
	add     x2, x2, x1                  // address of this CPU's slot
	ldr     x3, [x2]                    // load entry pointer
	cbz     x3, 1f                      // if zero, still parked
	str     xzr, [x2]                   // clear the slot before jumping to it
	br      x3                           // branch to the requested entry
1:
	wfe                                 // wait for wake up event
	b       proc_hang

.globl secondary_boot
secondary_boot:
	// Select a temporary low-memory stack dedicated to this CPU.
	mrs     x0, mpidr_el1               // read processor id register
	and     x0, x0, #0xFF               // keep lowest byte as CPU index
	ldr     x1, =secondary_boot_stacks  // base of temporary per-CPU boot stacks
	lsl     x2, x0, #12                 // cpu_index * 4096
	add     x1, x1, x2                  // this CPU's temporary stack base
	add     x1, x1, #SECONDARY_BOOT_STACK_SIZE // top of the temporary stack

	// Re-run the EL drop path for the secondary so it reaches EL1 cleanly.
	mrs     x0, CurrentEL               // get current EL
	and     x0, x0, #12                 // keep EL bits only
	cmp     x0, #12                     // running at EL3?
	bne     secondary_el2_entry

secondary_el3_entry:
	mov     x2, #0x5b1                  // configure SCR_EL3 for lower-level boot
	msr     scr_el3, x2                 // install EL3 control bits
	mov     x2, #0x3c9                  // configure SPSR_EL3 for EL2 return
	msr     spsr_el3, x2                // save return state
	adr     x2, secondary_el2_entry     // next step after ERET
	msr     elr_el3, x2                 // set EL3 return target
	eret                                 // drop from EL3 to EL2/EL1 path

secondary_el2_entry:
	cmp     x0, #4                      // already at EL1?
	beq     secondary_el1_entry
	msr     sp_el1, x1                  // seed EL1 stack for the return path

	// Enable EL1 timer access and clear EL2 trap state.
	mrs     x0, cnthctl_el2             // read EL2 timer control
	orr     x0, x0, #3                  // enable CNTP access for EL1
	msr     cnthctl_el2, x0             // install timer access bits
	msr     cntvoff_el2, xzr            // clear virtual counter offset
	mov     x0, #0x33FF                 // disable EL2 coprocessor traps
	msr     cptr_el2, x0                // install EL2 coprocessor policy
	msr     hstr_el2, xzr               // disable CP15 traps to EL2
	mov     x0, #(3 << 20)              // enable FP/SIMD in EL1
	msr     cpacr_el1, x0               // install EL1 FP/SIMD access
	isb                                 // synchronize system register state

	// Configure EL2 so ERET enters AArch64 EL1.
	mov     x0, #(1 << 31)              // select AArch64 for EL1
	orr     x0, x0, #(1 << 1)           // keep SWIO bit expected on Pi3
	msr     hcr_el2, x0                 // install EL2 control state
	mov     x2, #0x0800                 // low half of SCTLR_EL1 value
	movk    x2, #0x30d0, lsl #16        // high half of SCTLR_EL1 value
	msr     sctlr_el1, x2               // seed EL1 SCTLR before return
	mov     x2, #(0x3c4 + 1)            // use EL1h with SPSel
	msr     spsr_el2, x2                // save EL2 return state
	adr     x2, secondary_el1_entry     // next step after ERET
	msr     elr_el2, x2                 // set EL2 return target
	msr     elr_el1, xzr                // clear stale EL1 exception link
	msr     far_el1, xzr                // clear stale fault address state
	eret                                 // enter EL1 for secondary boot

secondary_el1_entry:
	mov     sp, x1                      // use temporary low-memory stack in EL1
	bl      boot_init_mmu               // enable MMU and higher-half mappings

	// Move from the temporary boot stack to the permanent per-CPU stack.
	mrs     x0, mpidr_el1               // read processor id register
	and     x0, x0, #0xFF               // keep lowest byte as CPU index
	bl      percpu_stack_top            // get permanent kernel stack top for this CPU
	mov     sp, x0                      // activate the permanent per-CPU stack

	// Enter the higher-half C secondary entry point.
	bl      secondary_start             // continue secondary initialization in C
	b       .                           // secondary_start should not return

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
