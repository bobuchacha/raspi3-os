#include "macros.S"
#include "arch/cortex-a53/irq.h"
#include "memory.h"
#include "syscall.h"

#define ESR_ELx_EC_SHIFT		26
#define ESR_ELx_EC_SVC64		0x15
#define ESR_ELx_EC_DABT_LOW		0x24
#define S_X0					0		// offset of x0 register in saved stack frame

FUNCTION irq_vector_init
	adr	x0, vector_table		    // load VBAR_EL1 with virtual
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
.globl vector_table
.align 11
vector_table:
	ventry	sync_invalid_el1t			// Synchronous EL1t
	ventry	irq_invalid_el1t //el1_irq						// IRQ EL1t
	ventry	fiq_invalid_el1t			// FIQ EL1t
	ventry	error_invalid_el1t			// Error EL1t

	ventry	sync_invalid_el1h			// Synchronous EL1h
	ventry	el1_irq						// IRQ EL1h
	ventry	fiq_invalid_el1h			// FIQ EL1h
	ventry	error_invalid_el1h			// Error EL1h

	ventry	el0_sync					// Synchronous 64-bit EL0
	ventry	el0_irq						// IRQ 64-bit EL0
	ventry	fiq_invalid_el0_64			// FIQ 64-bit EL0
	ventry	error_invalid_el0_64			// Error 64-bit EL0

	ventry	sync_invalid_el0_32			// Synchronous 32-bit EL0
	ventry	irq_invalid_el0_32			// IRQ 32-bit EL0
	ventry	fiq_invalid_el0_32			// FIQ 32-bit EL0
	ventry	error_invalid_el0_32			// Error 32-bit EL0

//-------------------------INVALID HANDLER CALLEE----------------------------

sync_invalid_el1t:
	handle_invalid_entry  1 SYNC_INVALID_EL1t

irq_invalid_el1t:
	handle_invalid_entry  1 IRQ_INVALID_EL1t

fiq_invalid_el1t:
	handle_invalid_entry  1 FIQ_INVALID_EL1t

error_invalid_el1t:
	handle_invalid_entry  1 ERROR_INVALID_EL1t

sync_invalid_el1h:
	handle_invalid_entry  1 SYNC_INVALID_EL1h

fiq_invalid_el1h:
	handle_invalid_entry  1 FIQ_INVALID_EL1h

error_invalid_el1h:
	handle_invalid_entry  1 ERROR_INVALID_EL1h

sync_invalid_el0_64:
	handle_invalid_entry  0 SYNC_INVALID_EL0_64

irq_invalid_el0_64:
	handle_invalid_entry  0 IRQ_INVALID_EL0_64

fiq_invalid_el0_64:
	handle_invalid_entry  0 FIQ_INVALID_EL0_64

error_invalid_el0_64:
	handle_invalid_entry  0 ERROR_INVALID_EL0_64

sync_invalid_el0_32:
	handle_invalid_entry  0 SYNC_INVALID_EL0_32

irq_invalid_el0_32:
	handle_invalid_entry  0 IRQ_INVALID_EL0_32

fiq_invalid_el0_32:
	handle_invalid_entry  0 FIQ_INVALID_EL0_32

error_invalid_el0_32:
	handle_invalid_entry  0 ERROR_INVALID_EL0_32

//-----------------------------KERNEL TIMER IRQ------------------------------
el1_irq:								// kernel timer IRQ
	kernel_entry 1
	bl		handle_irq
	kernel_exit 1

el0_irq:								// kernel timer IRQ
	kernel_entry 0
	bl		handle_irq
	kernel_exit 0


//-----------------------------EL0 SYNC HANDLER-----------------------------
syscall_num			.req	x26					// syscall number
syscall_table_ptr	.req	x27					// syscall table pointer

el0_sync:
	kernel_entry 0

	mrs		x25, esr_el1					// read the syndrome register
	lsr		x24, x25, #ESR_ELx_EC_SHIFT		// exception class
	cmp		x24, #ESR_ELx_EC_SVC64			// SVC in 64-bit state
	b.eq	el0_svc

	cmp		x24, #ESR_ELx_EC_DABT_LOW		// data abort in EL0
	b.eq	el0_da

	handle_invalid_entry 0, SYNC_ERROR		// invalid sync exception

//---------------------------SERVICE CALL HANDLER---------------------------
el0_svc:
	// load the low 16 bit in ESR into w8
	mrs		syscall_num, esr_el1
	and 	syscall_num, syscall_num, #0xFFFF	
	
	adr		syscall_table_ptr, sys_call_table					// load syscall table pointer
	mov		syscall_num, x8

	bl		enable_irq
	cmp     syscall_num, #SYS_NUM_OF_ENTRIES                    // check upper syscall limit
	b.hs	ni_sys

	ldr		x16, [syscall_table_ptr, syscall_num, lsl #3]		// address in the syscall table
	blr		x16													// call sys_* routine
	b		ret_from_syscall

ni_sys:
	handle_invalid_entry 0, SYSCALL_ERROR						// invalid sys call request

ret_from_syscall:						// we are going to switch back to user mode from here. So everything needs to be structured
	bl		disable_irq					// so kernel_exit can return it to user process
	str		x0, [sp, #S_X0]				// returned x0
	kernel_exit 0

//---------------------------DATA ABORT HANDLER---------------------------
el0_da:
	bl		enable_irq
	mrs		x0, far_el1
	mrs		x1, esr_el1			
	bl		mem_handle_data_abort		// tell mem management to handle it

	cmp 	x0, 0						// if the return is 0 - success. otherwise, show message
	b.eq 	1f
	handle_invalid_entry 0, DATA_ABORT_ERROR

1:
	bl 		disable_irq				
	kernel_exit 0