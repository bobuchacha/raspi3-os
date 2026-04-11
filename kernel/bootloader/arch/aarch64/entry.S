.section .text.boot, "ax"

.global _start

.extern __bss_start
.extern __bss_end
.extern __stack_top

_start:
    ldr x1, =__stack_top
    mrs x0, CurrentEL
    and x0, x0, #12
    cmp x0, #12
    b.eq 1f
    b 2f

1:  // EL3 -> EL2
    mov x2, #0x5b1
    msr scr_el3, x2
    mov x2, #0x3c9
    msr spsr_el3, x2
    adr x2, 2f
    msr elr_el3, x2
    eret

2:  // EL2 -> EL1 when needed
    cmp x0, #4
    b.eq 3f
    msr sp_el1, x1
    mrs x0, cnthctl_el2
    orr x0, x0, #3
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr
    mov x0, #0x33ff
    msr cptr_el2, x0
    msr hstr_el2, xzr
    mov x0, #(3 << 20)
    msr cpacr_el1, x0
    isb
    mov x0, #(1 << 31)
    orr x0, x0, #(1 << 1)
    msr hcr_el2, x0
    mov x2, #0x0800
    movk x2, #0x30d0, lsl #16
    msr sctlr_el1, x2
    mov x2, #(0x3c4 + 1)
    msr spsr_el2, x2
    adr x2, 3f
    msr elr_el2, x2
    msr elr_el1, xzr
    msr far_el1, xzr
    eret

3:
    mov sp, x1

// zero bss
    ldr x0, =__bss_start        // Load the address of the start of the BSS section into x0
    ldr x1, =__bss_end          // Load the address of the end of the BSS section into x1

4:
    cmp x0, x1                  // Compare the current address with the end of the BSS section  
    b.hs 5f                     // branch to 5 if higher or same
    strb wzr, [x0], #1          // Store zero at the current address and increment the address
    b 4b                        // repeat back to 4

5:
    bl boot_main                // jump to boot_main in bootloader.c

6:
    wfe
    b 6b                        // halt
