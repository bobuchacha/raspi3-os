// save registers before calling debugger code in C
.globl dbg_saveregs
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