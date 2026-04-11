.section .text.boot, "ax"

.global kernel_entry

.extern __bss_start
.extern __bss_end
.extern __stack_top

kernel_entry:
.setup_stack:
    ldr x0, =__stack_top        // setup stack
    mov sp, x0

.clear_bss:
    ldr x0, =__bss_start        
    ldr x1, =__bss_end

1:
    cmp x0, x1
    b.hs 2f
    strb wzr, [x0], #1
    b 1b

2:
    bl kernel_main

3:
    wfe
    b 3b
