#include "memory.h"
#include "boot-handoff.h"
#include "../macros.S"

.section ".text.boot"

.equ SECONDARY_BOOT_STACK_SIZE, 4096

.globl _start
_start:
	mrs	    x0, mpidr_el1
	and	    x0, x0,#0xFF
    cbz	    x0, master

proc_hang:
	mrs     x1, mpidr_el1
	and     x1, x1, #0xFF
	ldr     x2, =secondary_entry_table
	lsl     x1, x1, #3
	add     x2, x2, x1
	ldr     x3, [x2]
	cbz     x3, 1f
	str     xzr, [x2]
	br      x3
1:
	wfe
	b       proc_hang

.globl secondary_boot
secondary_boot:
	mrs     x0, mpidr_el1
	and     x0, x0, #0xFF
	ldr     x1, =secondary_boot_stacks
	lsl     x2, x0, #12
	add     x1, x1, x2
	add     x1, x1, #SECONDARY_BOOT_STACK_SIZE

	mrs     x0, CurrentEL
	and     x0, x0, #12
	cmp     x0, #12
	bne     secondary_el2_entry

secondary_el3_entry:
	mov     x2, #0x5b1
	msr     scr_el3, x2
	mov     x2, #0x3c9
	msr     spsr_el3, x2
	adr     x2, secondary_el2_entry
	msr     elr_el3, x2
	eret

secondary_el2_entry:
	cmp     x0, #4
	beq     secondary_el1_entry
	msr     sp_el1, x1
	mrs     x0, cnthctl_el2
	orr     x0, x0, #3
	msr     cnthctl_el2, x0
	msr     cntvoff_el2, xzr
	mov     x0, #0x33FF
	msr     cptr_el2, x0
	msr     hstr_el2, xzr
	mov     x0, #(3 << 20)
	msr     cpacr_el1, x0
	isb
	mov     x0, #(1 << 31)
	orr     x0, x0, #(1 << 1)
	msr     hcr_el2, x0
	mov     x2, #0x0800
	movk    x2, #0x30d0, lsl #16
	msr     sctlr_el1, x2
	mov     x2, #(0x3c4 + 1)
	msr     spsr_el2, x2
	adr     x2, secondary_el1_entry
	msr     elr_el2, x2
	msr     elr_el1, xzr
	msr     far_el1, xzr
	eret

secondary_el1_entry:
	mov     sp, x1
	bl      boot_init_mmu
	ldr     x0, =(VA_START + ROS_BOOT_HANDOFF_PHYS_ADDR)
	ldr     x1, [x0, #ROS_BOOT_HANDOFF_OFFSET_SECONDARY_ENTRY_POINT]
	cbz     x1, 1f
	br      x1
1:
	wfe
	b       1b

master:
    ldr     x1, =_start
	mrs     x0, CurrentEL
	and     x0, x0, #12
	cmp     x0, #12
	bne     el2_entry

el3_entry:
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
    msr     sp_el1, x1
	mrs     x0, cnthctl_el2
	orr     x0, x0, #3
	msr     cnthctl_el2, x0
	msr     cntvoff_el2, xzr
	mov     x0, #0x33FF
	msr     cptr_el2, x0
	msr     hstr_el2, xzr
	mov     x0, #(3 << 20)
	msr     cpacr_el1, x0
	isb
	mov     x0, #(1 << 31)
	orr     x0, x0, #(1 << 1)
	msr     hcr_el2, x0
	mov     x2, #0x0800
	movk    x2, #0x30d0, lsl #16
	msr     sctlr_el1, x2
	mov     x2, #(0x3c4 + 1)
	msr     spsr_el2, x2
	adr     x2, el1_entry
	msr     elr_el2, x2
	msr     elr_el1, xzr
	msr     far_el1, xzr
	eret

el1_entry:
	mov     sp, x1
	bl		uart1_init
	bl		boot_init_mmu
	mov 	x1, #VA_START
	add		sp, x1, #LOW_MEMORY_CEILING
	bl 		clear_bss

jump_to_kernel_main:
    ldr x10, =kernel_main
    br x10

    b proc_hang

clear_bss:
	ldr     x1, =bss_begin
	ldr     w2, =bss_size
3:
    cbz     w2, 4f
	str     xzr, [x1], #8
	sub     w2, w2, #1
	cbnz    w2, 3b
4:
	ret

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
