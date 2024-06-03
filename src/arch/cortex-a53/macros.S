#define MACRO .macro
#define ENDM .endm

MACRO FUNCTION name
	.globl \name
	\name:
ENDM

.macro ENDFUNC name

.endm

.macro ENDF
    ret
.endm

.macro POPP r1, r2
    ldp   \r1, \r2, [sp], #16
.endm

.macro PUSHP r1, r2
    stp   \r1, \r2, [sp, #-16]!
.endm

.macro POP reg
    ldr   \reg, [sp], #8
.endm

.macro PUSH reg
    str   \reg, [sp, #-8]!
   // str   \reg, [sp], #-8
.endm

/*
 * this macro to reserve stacks for specific bytes. Usually align 16 bytes, for mstack storing manually
 */
.macro RESERVE_STACK bytes
    sub		sp, sp, #\bytes
.endm

.macro FREE_STACK bytes
    add		sp, sp, #\bytes
.endm

.macro HANDLE_INVALID_ENTRY type		// this handles invalid entry by calling our debugger
	bl      dbg_saveregs				// save registers before calling C code of debugger

	mov		x0, #\type
	mrs		x1, esr_el1
	mrs		x2, elr_el1
	bl		show_invalid_entry_message

    bl      dbg_decodeexc
    bl      dbg_main
	eret                            // this has to be called when returned from dbg_main because it increase the PC by 1 instruction. So kernel will get to the next instruction
.endm

.macro	VENTRY	label					// this macro creates a short code that jumps to label ò handler
	.align	7
	b		\label
.endm

.macro	KERNEL_ENTRY el
    PUSHP x0, x1
    PUSHP x2, x3
    PUSHP x4, x5
    PUSHP x6, x7
    PUSHP x8, x9
    PUSHP x10, x11
    PUSHP x12, x13
    PUSHP x14, x15
    PUSHP x16, x17
    PUSHP x18, x19
    PUSHP x20, x21
    PUSHP x22, x23
    PUSHP x24, x25
    PUSHP x26, x27
    PUSHP x28, x29

    .if	\el == 0
    	mrs	x21, sp_el0             // EL0: save EL0 SP into x21
    .else
    	add	x21, sp, 16*15          // EL1: x21 = our SP. Point to x0 above. since we have decreased sp 15 times of 16 bytes by calling PUSHP 15 times.
    .endif

    mrs	x22, elr_el1
    mrs	x23, spsr_el1

    PUSHP	x30, x21            // push x30, sp
    PUSHP	x22, x23            // push ELR, SPSR
.endm

.macro KERNEL_EXIT el
    POPP x22, x23        // x22 = ELR, x23 = SPSR
    POPP x30, x21        // x21 = SP

	.if	\el == 0
	msr	sp_el0, x21     // restore SP
	.endif              // for EL1, we dont need to set our SP, because after pop all values, sp will restore

	msr	elr_el1, x22
	msr	spsr_el1, x23

	POPP x28, x29
	POPP x26, x27
	POPP x24, x25
	POPP x22, x23
	POPP x20, x21
	POPP x18, x19
	POPP x16, x17
	POPP x14, x15
	POPP x12, x13
	POPP x10, x11
	POPP x8, x9
	POPP x6, x7
	POPP x4, x5
	POPP x2, x3
	POPP x0, x1
	eret
.endm