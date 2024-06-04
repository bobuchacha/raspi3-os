#define MACRO .macro
#define ENDM .endm

MACRO FUNCTION name
	.globl \name
	\name:
ENDM

.macro VAR name
	.globl \name
	\name:
.endm

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


.macro PRINT_REG name newline
    PUSHP x0, x1
    PUSHP x2, x3

    PUSHP x0, x1
    ldr x0, =0f
    bl uart0_puts
    POPP x0, x1

    mov x0, \name
    bl uart0_lhex

    ldr x0, =1f
    bl uart0_puts

    POPP x2, x3
    POPP x0, x1

    b 2f
    0: .asciz "\name :"
    1: .asciz "\newline"
    .align 2
    2:
.endm

.macro PRINT_SYSREG name newline
    PUSHP x0, x1
    PUSHP x2, x3

    PUSHP x0, x1    
    ldr x0, =0f
    bl uart0_puts   // print register name
    POPP x0, x1

    mrs x0, \name
    bl uart0_lhex   // print its value

    ldr x0, =1f
    bl uart0_puts   // print new line

    POPP x2, x3
    POPP x0, x1

    b 2f
    0: .asciz "\name :"
    1: .asciz "\newline"
    .align 2
    2:
.endm


.macro PRINTS string
    PUSHP x0, x1    
    ldr x0, =6f
    bl uart0_puts   // print register name
    POPP x0, x1

    b 9f
    6: .asciz "\string"
    .align 2
    9:
.endm


.macro DUMP_REGISTERS
    PRINTS "-------------------------------- [REGISTERS] ---------------------------------\n"

    PRINT_REG "x0  " "      "
    PRINT_REG "x1  " "      "
    PRINT_REG "x2  " "\n"
    PRINT_REG "x3  " "      "
    PRINT_REG "x4  " "      "
    PRINT_REG "x5  " "\n"
    PRINT_REG "x6  " "      "
    PRINT_REG "x7  " "      "
    PRINT_REG "x8  " "\n"
    PRINT_REG "x9  " "      "
    PRINT_REG "x10 " "      "
    PRINT_REG "x11 " "\n"
    PRINT_REG "x12 " "      "
    PRINT_REG "x13 " "      "
    PRINT_REG "x14 " "\n"
    PRINT_REG "x15 " "      "
    PRINT_REG "x16 " "      "
    PRINT_REG "x17 " "\n"
    PRINT_REG "x18 " "      "
    PRINT_REG "x19 " "      "
    PRINT_REG "x20 " "\n"
    PRINT_REG "x21 " "      "
    PRINT_REG "x22 " "      "
    PRINT_REG "x23 " "\n"
    PRINT_REG "x24 " "      "
    PRINT_REG "x25 " "      "
    PRINT_REG "x26 " "\n"
    PRINT_REG "x27 " "      "
    PRINT_REG "x28 " "      "
    PRINT_REG "x29 " "\n"
    PRINT_REG "x30 " "      "
    PRINT_REG "sp  " "\n"
    PRINT_SYSREG "elr_el1  " "     "
	PRINT_SYSREG "spsr_el1 " "\n"
    PRINT_SYSREG "far_el1  " "\n"
    PRINT_SYSREG "sp_el0   " "\n"
	
    PRINTS "------------------------------------------------------------------------------\n"
    
.endm




#define S_FRAME_SIZE 16*17
.macro	KERNEL_ENTRY el
    // PRINTS "\n>> ASM: KERNEL_ENTRY CALLED\n"
    // DUMP_REGISTERS

    // PUSHP x0, x1
    // PUSHP x2, x3
    // PUSHP x4, x5
    // PUSHP x6, x7
    // PUSHP x8, x9
    // PUSHP x10, x11
    // PUSHP x12, x13
    // PUSHP x14, x15
    // PUSHP x16, x17
    // PUSHP x18, x19
    // PUSHP x20, x21
    // PUSHP x22, x23
    // PUSHP x24, x25
    // PUSHP x26, x27
    // PUSHP x28, x29

    // .if	\el == 0
    // 	mrs	x21, sp_el0             // EL0: save EL0 SP into x21
    // .else
    // 	add	x21, sp, 16*15          // EL1: x21 = our SP. Point to x0 above. since we have decreased sp 15 times of 16 bytes by calling PUSHP 15 times.
    // .endif

    // mrs	x22, elr_el1
    // mrs	x23, spsr_el1

    // PUSHP	x30, x21            // push x30, sp
    // PUSHP	x22, x23            // push ELR, SPSR
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

	.if	\el == 0
	mrs	x21, sp_el0
	.else
	add	x21, sp, #S_FRAME_SIZE
	.endif /* \el == 0 */

	mrs	x22, elr_el1
	mrs	x23, spsr_el1

	stp	x30, x21, [sp, #16 * 15] 
	stp	x22, x23, [sp, #16 * 16]

.endm

.macro KERNEL_EXIT el
    // POPP x22, x23        // x22 = ELR, x23 = SPSR
    // POPP x30, x21        // x21 = SP

	// .if	\el == 0
	// msr	sp_el0, x21     // restore SP
	// .endif              // for EL1, we dont need to set our SP, because after pop all values, sp will restore

	// msr	elr_el1, x22
	// msr	spsr_el1, x23

	// POPP x28, x29
	// POPP x26, x27
	// POPP x24, x25
	// POPP x22, x23
	// POPP x20, x21
	// POPP x18, x19
	// POPP x16, x17
	// POPP x14, x15
	// POPP x12, x13
	// POPP x10, x11
	// POPP x8, x9
	// POPP x6, x7
	// POPP x4, x5
	// POPP x2, x3
	// POPP x0, x1
    ldp	x22, x23, [sp, #16 * 16]        // pc = x22, state = x23
	ldp	x30, x21, [sp, #16 * 15]        // sp = x21

	.if	\el == 0
	msr	sp_el0, x21
	.endif /* \el == 0 */

	msr	elr_el1, x22			
	msr	spsr_el1, x23


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
	add	sp, sp, #S_FRAME_SIZE		
	
    // PRINTS "\n\n>>ASM: KERNEL_EXIT\n\n"
    
    // // PRINTS "\n>> ASM: KERNEL_EXIT CALLED\n"
    // DUMP_REGISTERS
	
	eret
.endm
