#include "task.h"
#include "macros.S"

FUNCTION cpu_switch_to 	            // x0 = prev task, x1 = next task

	mov	x10, #THREAD_CPU_CONTEXT    // Where in the task struct the regs (or context start)
	add	x8, x0, x10			        // cpu context into x8
	mov	x9, sp				        // save current sp into x9 and restore later
	stp	x19, x20, [x8], #16		    // store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	stp	x29, x9, [x8], #16			// store x29, sp
	str	x30, [x8]					// store x30
	
	add	x8, x1, x10			        // now x8 is next task's regs. Load into sp reg
	ldp	x19, x20, [x8], #16		    // restore callee-saved registers
	ldp	x21, x22, [x8], #16
	ldp	x23, x24, [x8], #16
	ldp	x25, x26, [x8], #16
	ldp	x27, x28, [x8], #16
	ldp	x29, x9, [x8], #16
	ldr	x30, [x8]
	mov	sp, x9
ENDF
	

FUNCTION ret_from_fork

	// PRINTS "\nret_from_fork: Forking new process...\n\n"

	bl	    schedule_tail
	cbz 	x19, ret_to_user		// not a kernel thread
	mov 	x0, x20
	mov 	x1, x21
	mov 	x2, x22
	blr 	x19
	/* thread exit here. Should we do some thing here? */
	
	//mov 	x0, xzr
	//bl 	exit_current_process

	//PRINTS "\n\n===============\nTHREAD ENDED. SHOULD IMPLEMENT SOMETHING TO END THE THREAD. THREAD RUN INTO LOOP NOW\n===============\n\n"
	//b .
ret_to_user:
	// PRINTS "\nret_from_fork: Returning to user...\n\n"
	bl      disable_irq
	kernel_exit 0 
ENDFUNC


FUNCTION test_asm_prints
	mov x0, #0xdead
	mov x1, #0xcabe
	PRINT_REG "sp" "<--- before storing\n"
	PRINT_REG "x0" "<--- before storing\n"
	PRINT_REG "x1" "<--- before storing\n\n----\n\n"
	// stp 	x0, x1, [sp, #-16]!
	PUSHP x0, x1

	mov x0, #0x1234
	mov x1, #0x5678

	PRINT_REG "sp" "<--- after storing\n"
	PRINT_REG "x0" "<--- after storing\n"
	PRINT_REG "x1" "<--- after storing\n\n----\n\n"

	// ldp 	x0, x1, [sp], #16
	POPP x0, x1

 	PRINT_REG "sp" "<--- after loadp\n"	
	PRINT_REG "x0" "<--- after pop\n"
	PRINT_REG "x1" "<--- after pop\n"
	
ENDF


FUNCTION test_uxtw
	PUSHP x29, x30
	mov x0, xzr
	mov x1, xzr

	movk x0, 0xFFFF, lsl #48
	movk x0, 0xDEAD, lsl #32
	movk x0, 0x1234, lsl #16
	movk x0, 0x5678 //Implies lsl #0

	PRINT_REG "x0" "<--- b4 storing\n"
	PRINT_REG "x1" "<--- b4 storing\n-------------\n"
	uxtw x1, w0		// <---- lo32 xo to work
	PRINT_REG "x0" "<--- after storing\n"
	PRINT_REG "x1" "<--- after storing\n-------------\n"

	POPP x29, x30
ENDF