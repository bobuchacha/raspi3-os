#include "macros.S"
#include "arch/cortex-a53/irq.h"

#define ESR_ELx_EC_SHIFT		26
#define ESR_ELx_EC_SVC64		0x15
#define ESR_ELx_EC_DABT_LOW		0x24
#define S_FRAME_SIZE			16*17 		// size of all saved registers
#define S_X0				0		// offset of x0 register in saved stack frame

FUNCTION irq_vector_init
	adr	x0, irq_vector_table		    // load VBAR_EL1 with virtual
	msr	vbar_el1, x0		            // vector table address
ENDF

FUNCTION enable_irq
	msr    daifclr, #2
ENDF

FUNCTION disable_irq
	msr	daifset, #2
ENDF

//-----------------------------------------------------------------------
// Our vector starts here
// if we run into any exception that does not have handler yet,
// pass it to debugger
//-----------------------------------------------------------------------
.globl irq_vector_table
.align 11	// this vector table has to be aligned as well
irq_vector_table:
	ventry	sync_invalid_el1t			// Synchronous EL1t
	ventry	el1_irq					// IRQ EL1t
	ventry	fiq_invalid_el1t			// FIQ EL1t
	ventry	error_invalid_el1t			// Error EL1t

	ventry	sync_invalid_el1h			// Synchronous EL1h
	ventry	el1_irq					// IRQ EL1h
	ventry	fiq_invalid_el1h			// FIQ EL1h
	ventry	error_invalid_el1h			// Error EL1h

	ventry	sync_invalid_el0_64			// Synchronous 64-bit EL0
	ventry	irq_invalid_el0_64			// IRQ 64-bit EL0
	ventry	fiq_invalid_el0_64			// FIQ 64-bit EL0
	ventry	error_invalid_el0_64			// Error 64-bit EL0

	ventry	sync_invalid_el0_32			// Synchronous 32-bit EL0
	ventry	irq_invalid_el0_32			// IRQ 32-bit EL0
	ventry	fiq_invalid_el0_32			// FIQ 32-bit EL0
	ventry	error_invalid_el0_32			// Error 32-bit EL0

//-------------------------INVALID HANDLER CALLEE----------------------------

sync_invalid_el1t:
	handle_invalid_entry  SYNC_INVALID_EL1t

irq_invalid_el1t:
	handle_invalid_entry  IRQ_INVALID_EL1t

fiq_invalid_el1t:
	handle_invalid_entry  FIQ_INVALID_EL1t

error_invalid_el1t:
	handle_invalid_entry  ERROR_INVALID_EL1t

sync_invalid_el1h:
	handle_invalid_entry  SYNC_INVALID_EL1h

fiq_invalid_el1h:
	handle_invalid_entry  FIQ_INVALID_EL1h

error_invalid_el1h:
	handle_invalid_entry  ERROR_INVALID_EL1h

sync_invalid_el0_64:
	handle_invalid_entry  SYNC_INVALID_EL0_64

irq_invalid_el0_64:
	handle_invalid_entry  IRQ_INVALID_EL0_64

fiq_invalid_el0_64:
	handle_invalid_entry  FIQ_INVALID_EL0_64

error_invalid_el0_64:
	handle_invalid_entry  ERROR_INVALID_EL0_64

sync_invalid_el0_32:
	handle_invalid_entry  SYNC_INVALID_EL0_32

irq_invalid_el0_32:
	handle_invalid_entry  IRQ_INVALID_EL0_32

fiq_invalid_el0_32:
	handle_invalid_entry  FIQ_INVALID_EL0_32

error_invalid_el0_32:
	handle_invalid_entry  ERROR_INVALID_EL0_32

//-----------------------------KERNEL TIMER IRQ------------------------------
el1_irq:								// kernel timer IRQ
	kernel_entry 0
	bl		handle_irq
	kernel_exit 0

// //---------------------------------------------------------------------------
// el1_sync:
// 	kernel_entry 0
// 	mrs    	x25, esr_el1                // read the syndrome register
//     lsr    	x24, x25, #ESR_ELx_EC_SHIFT // exception class
//     cmp    	x24, #ESR_ELx_EC_SVC64      // SVC in 64-bit state
//     b.eq   	el1_svc
// 	bl 		enable_irq
// 	kernel_exit 0

// sc_nr   .req    x25                  // number of system calls
// scno    .req    x26                  // syscall number
// stbl    .req    x27                  // syscall table pointer


// //---------------------------------------------------------------------------
// el0_sync:		// note - breakpoint also here
// 	kernel_entry 1

// 	mrs    x25, esr_el1                // read the syndrome register
//     lsr    x24, x25, #ESR_ELx_EC_SHIFT // exception class
//     cmp    x24, #ESR_ELx_EC_SVC64      // SVC in 64-bit state
//     b.eq   el0_svc
// handle_invalid_entry SYNC_ERROR

// _invalid:
// 	mov		x0, #SYNC_ERROR
// 	mrs		x1, esr_el1
// 	mrs		x2, elr_el1
// 	bl		show_invalid_entry_message
// 	b 		ret_from_syscall

// sc_nr	.req	x25					// number of system calls
// scno	.req	x26					// syscall number
// stbl	.req	x27					// syscall table pointer

// //-----------------
// // handles service call from el0
// el0_svc:
// 	adr	stbl, sys_call_table			// load syscall table pointer
// 	uxtw	scno, w8				// syscall number in w8
// 	mov	sc_nr, #1
// 	bl	enable_irq
// 	cmp     scno, sc_nr                     	// check upper syscall limit
// 	b.hs	ni_sys

// 	ldr	x16, [stbl, scno, lsl #3]		// address in the syscall table
// 	blr	x16					// call sys_* routine
// 	b	ret_from_syscall
// ni_sys:
// 	handle_invalid_entry SYSCALL_ERROR
// ret_from_syscall:
// 	bl	disable_irq				
// 	str	x0, [sp, #S_X0]				// returned x0
// 	kernel_exit 0
// .global test_string
// string_err: 				.asciz "\n\r\n\rError!\n\r\n\r"
// string_not_implemented:		.asciz "\nService call not implemented!\n"
