#include "entry.h"

	.macro handle_invalid_entry type
	kernel_entry
	mov	x0, #\type
	mrs	x1, esr_el1
	mrs	x2, elr_el1
	bl	show_invalid_entry_message
	b	err_hang
	.endm

	.macro	ventry	label
	.align	7
	b	\label
	.endm

	.macro	kernel_entry
	sub	sp, sp, #S_FRAME_SIZE
	stp	x0, x1, [sp, #16 * 0]
	stp	x2, x3, [sp, #16 * 1]
	stp	x4, x5, [sp, #16 * 2]
	stp	x6, x7, [sp, #16 * 3]
	stp	x8, x9, [sp, #16 * 4]
	stp	x10, x11, [sp, #16 * 5]
	stp	x12, x13, [sp, #16 * 6]
	stp	x14, x15, [sp, #16 * 7]
	stp	x16, x17, [sp, #16 * 8]
	stp	x18, x19, [sp, #16 * 9]
	stp	x20, x21, [sp, #16 * 10]
	stp	x22, x23, [sp, #16 * 11]
	stp	x24, x25, [sp, #16 * 12]
	stp	x26, x27, [sp, #16 * 13]
	stp	x28, x29, [sp, #16 * 14]
	str	x30, [sp, #16 * 15] 
	.endm

	.macro	kernel_exit
	ldp	x0, x1, [sp, #16 * 0]
	ldp	x2, x3, [sp, #16 * 1]
	ldp	x4, x5, [sp, #16 * 2]
	ldp	x6, x7, [sp, #16 * 3]
	ldp	x8, x9, [sp, #16 * 4]
	ldp	x10, x11, [sp, #16 * 5]
	ldp	x12, x13, [sp, #16 * 6]
	ldp	x14, x15, [sp, #16 * 7]
	ldp	x16, x17, [sp, #16 * 8]
	ldp	x18, x19, [sp, #16 * 9]
	ldp	x20, x21, [sp, #16 * 10]
	ldp	x22, x23, [sp, #16 * 11]
	ldp	x24, x25, [sp, #16 * 12]
	ldp	x26, x27, [sp, #16 * 13]
	ldp	x28, x29, [sp, #16 * 14]
	ldr	x30, [sp, #16 * 15] 
	add	sp, sp, #S_FRAME_SIZE		
	eret
	.endm


/*
 * Exception vectors.
 */
.align	7
.globl vectors 
vectors:
	ventry	sync_invalid_el1t			// Synchronous EL1t
	ventry	irq_invalid_el1t			// IRQ EL1t
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

el1_irq:
	kernel_entry 
	bl	handle_irq
	kernel_exit 
el_dummy:
    kernel_entry
    bl	handle_irq
    kernel_exit

.globl err_hang
err_hang: b err_hang

.globl ret_from_fork
ret_from_fork:
	bl	schedule_tail
	mov	x0, x20
	blr	x19 		//should never return


.global vectors_debugger
vectors_debugger:
    // synchronous
    .align  7
    str     x30, [sp, #-16]!     // push x30
    bl      dbg_saveregs
    mov     x0, #0
    bl      dbg_decodeexc
    bl      dbg_main
    eret
    //ventry el0_sync

    // IRQ
//    ventry	el1_irq					// IRQ EL1h
    ventry	el_dummy					// IRQ EL1h

    // FIQ
    .align  7
    str     x30, [sp, #-16]!     // push x30
    bl      dbg_saveregs
    mov     x0, #2
    bl      dbg_decodeexc
    bl      dbg_main
    eret

    // SError
    .align  7
    str     x30, [sp, #-16]!     // push x30
    bl      dbg_saveregs
    mov     x0, #3
    bl      dbg_decodeexc
    bl      dbg_main
    eret

    // Synchronous EL1h
    .align  7
    str     x30, [sp, #-16]!     // push x30
    bl      dbg_saveregs
    mov     x0, #3
    bl      dbg_decodeexc
    bl      dbg_main
    eret

    ventry	el1_irq					// IRQ EL1h

// save registers before we call any C code
dbg_saveregs:
    str     x0, [sp, #-16]!     // push x0
    ldr     x0, =dbg_regs+8
    str     x1, [x0], #8        // dbg_regs[1]=x1
    ldr     x1, [sp, #16]       // pop x1
    str     x1, [x0, #-16]!     // dbg_regs[0]=x1 (x0)
    add     x0, x0, #16
    str     x2, [x0], #8        // dbg_regs[2]=x2
    str     x3, [x0], #8        // ...etc.
    str     x4, [x0], #8
    str     x5, [x0], #8
    str     x6, [x0], #8
    str     x7, [x0], #8
    str     x8, [x0], #8
    str     x9, [x0], #8
    str     x10, [x0], #8
    str     x11, [x0], #8
    str     x12, [x0], #8
    str     x13, [x0], #8
    str     x14, [x0], #8
    str     x15, [x0], #8
    str     x16, [x0], #8
    str     x17, [x0], #8
    str     x18, [x0], #8
    str     x19, [x0], #8
    str     x20, [x0], #8
    str     x21, [x0], #8
    str     x22, [x0], #8
    str     x23, [x0], #8
    str     x24, [x0], #8
    str     x25, [x0], #8
    str     x26, [x0], #8
    str     x27, [x0], #8
    str     x28, [x0], #8
    str     x29, [x0], #8
    ldr     x1, [sp, #16]       // pop x30
    str     x1, [x0], #8
    // also read and store some system registers
    mrs     x1, elr_el1
    str     x1, [x0], #8
    mrs     x1, spsr_el1
    str     x1, [x0], #8
    mrs     x1, esr_el1
    str     x1, [x0], #8
    mrs     x1, far_el1
    str     x1, [x0], #8
    mrs     x1, sctlr_el1
    str     x1, [x0], #8
    mrs     x1, tcr_el1
    str     x1, [x0], #8
    ret
/*
el0_sync:
    kernel_entry 0
    mrs	x25, esr_el1				    // read the syndrome register
    lsr	x24, x25, #ESR_ELx_EC_SHIFT		// exception class
    cmp	x24, #ESR_ELx_EC_SVC64			// SVC in 64-bit state
    b.eq	el0_svc
    handle_invalid_entry 0, SYNC_ERROR
*/
sc_nr	.req	x25					// number of system calls
scno	.req	x26					// syscall number
stbl	.req	x27					// syscall table pointer