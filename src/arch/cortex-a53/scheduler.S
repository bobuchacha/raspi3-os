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
	stp	x29, x9, [x8], #16
	str	x30, [x8]
	
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
	bl	    schedule_tail
	cbz 	x19, ret_to_user		// not a kernel thread
	mov 	x0, x20
	blr 	x19
ret_to_user:
	bl      disable_irq
	KERNEL_EXIT 0
ENDFUNC
