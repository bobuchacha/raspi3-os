#include "arm/sysregs.h"
#include "memory.h"

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
    b entrypoint

signature:      padded_string "ROS.KRL\0", 8                                                // 8 byte signature
executable:     .word 0                                                                     // 4 bytes dummy flag
author:         padded_string "THANG CAO", 32
copyright:      padded_string "Copyright (c) 2024 Renaissance Pi OS", 64                    // 64 bytes
version:        padded_string "0.0.1", 32                                                   // 32 bytes

entrypoint:
	mrs	x1, mpidr_el1
	and	x1, x1, #0xFF		// Check processor id (0xFF)
	cbz	x1, master		// Hang for all non-primary CPU
	b	proc_hang

    // cpu id > 0, stop
proc_hang: 
	b 	proc_hang

    // cpu id == 0
master:

    // set top of stack just before our code (stack grows to a lower address per AAPCS64)
    ldr     x1, =_start

    // set up EL1
    mrs     x0, CurrentEL
    and     x0, x0, #12 // clear reserved bits

    // running at EL3?
    cmp     x0, #12
    bne     switch_to_EL1

    // should never be executed, just for completeness
    mov     x2, #0x5b1
    msr     scr_el3, x2
    mov     x2, #0x3c9
    msr     spsr_el3, x2
    adr     x2, _switch_to_EL1_continue
    msr     elr_el3, x2
    eret


switch_to_EL1:
    cmp     x0, #4
    beq     _switch_to_EL1_continue
    msr     sp_el1, x1

    // enable CNTP for EL1
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    // disable coprocessor traps
    mov     x0, #0x33FF
    msr     cptr_el2, x0
    msr     hstr_el2, xzr
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0

    // enable AArch64 in EL1
    mov     x0, #(1 << 31)      // AArch64
    orr     x0, x0, #(1 << 1)   // SWIO hardwired on Pi3
    msr     hcr_el2, x0
    mrs     x0, hcr_el2

    // Setup SCTLR access
    mov     x2, #0x0800
    movk    x2, #0x30d0, lsl #16
    msr     sctlr_el1, x2

    // set up exception handlers - moved to irq.S
    //ldr     x2, =_vectors_debugger
    //msr     vbar_el1, x2

    // change execution level to EL1
    mov     x2, #0x3c4
    msr     spsr_el2, x2
    adr     x2, _switch_to_EL1_continue
    msr     elr_el2, x2

    // clear EL1 system registers
    msr     elr_el1, xzr
    msr     far_el1, xzr

    eret

_switch_to_EL1_continue:
    mov     sp, x1

el1_entry:
	//adr	x0, bss_begin
	//adr	x1, bss_end
	//sub	x2, x1, x0
	//bl 	memzero             // zero out BSS

    //ldr     x1, =bss_begin
    //ldr     w2, =bss_size
//3:  cbz     w2, 4f
    //str     xzr, [x1], #8
    //sub     w2, w2, #1
    //cbnz    w2, 3b


4:
	mov	sp, #(2 * SECTION_SIZE)
	bl	kernel_main
	b 	proc_hang		// should never come here



