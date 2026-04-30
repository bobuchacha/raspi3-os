.section .text, "ax"

.equ CPU_CONTEXT_X19, (19 * 8)
.equ CPU_CONTEXT_X20, (20 * 8)
.equ CPU_CONTEXT_X21, (21 * 8)
.equ CPU_CONTEXT_X22, (22 * 8)
.equ CPU_CONTEXT_X23, (23 * 8)
.equ CPU_CONTEXT_X24, (24 * 8)
.equ CPU_CONTEXT_X25, (25 * 8)
.equ CPU_CONTEXT_X26, (26 * 8)
.equ CPU_CONTEXT_X27, (27 * 8)
.equ CPU_CONTEXT_X28, (28 * 8)
.equ CPU_CONTEXT_X29, (29 * 8)
.equ CPU_CONTEXT_SP,  (31 * 8)
.equ CPU_CONTEXT_USER_SP, (32 * 8)
.equ CPU_CONTEXT_PC,  (33 * 8)
.equ CPU_CONTEXT_STATE, (34 * 8)
.equ CPU_CONTEXT_EXCEPTION_FRAME, (35 * 8)

.equ EXC_FRAME_X0,      0
.equ EXC_FRAME_X2,      16
.equ EXC_FRAME_X4,      32
.equ EXC_FRAME_X6,      48
.equ EXC_FRAME_X8,      64
.equ EXC_FRAME_X10,     80
.equ EXC_FRAME_X12,     96
.equ EXC_FRAME_X14,     112
.equ EXC_FRAME_X16,     128
.equ EXC_FRAME_X18,     144
.equ EXC_FRAME_X20,     160
.equ EXC_FRAME_X22,     176
.equ EXC_FRAME_X24,     192
.equ EXC_FRAME_X26,     208
.equ EXC_FRAME_X28,     224
.equ EXC_FRAME_X30,     240
.equ EXC_FRAME_SP,      248
.equ EXC_FRAME_ELR,     256
.equ EXC_FRAME_SPSR,    264

.global aarch64_context_switch
.global aarch64_thread_entry

.extern scheduler_thread_exit_current

aarch64_context_switch:
    cbz x0, 1f

    stp x19, x20, [x0, #CPU_CONTEXT_X19]
    stp x21, x22, [x0, #CPU_CONTEXT_X21]
    stp x23, x24, [x0, #CPU_CONTEXT_X23]
    stp x25, x26, [x0, #CPU_CONTEXT_X25]
    stp x27, x28, [x0, #CPU_CONTEXT_X27]
    str x29, [x0, #CPU_CONTEXT_X29]

    mov x2, sp
    str x2, [x0, #CPU_CONTEXT_SP]
    str x30, [x0, #CPU_CONTEXT_PC]

1:
    ldp x19, x20, [x1, #CPU_CONTEXT_X19]
    ldp x21, x22, [x1, #CPU_CONTEXT_X21]
    ldp x23, x24, [x1, #CPU_CONTEXT_X23]
    ldp x25, x26, [x1, #CPU_CONTEXT_X25]
    ldp x27, x28, [x1, #CPU_CONTEXT_X27]
    ldr x29, [x1, #CPU_CONTEXT_X29]

    ldr x2, [x1, #CPU_CONTEXT_SP]
    mov sp, x2
    // A user thread can be descheduled while it is running inside EL1 during a
    // syscall or IRQ. When that happens, the later exception epilogue still
    // returns through ERET and consumes SP_EL0, so resuming only the kernel
    // stack is not enough: another thread may have already changed SP_EL0.
    // Restore the saved EL0 stack pointer for every resumed context here so
    // in-kernel continuations eventually return to the correct user frame.
    ldr x4, [x1, #CPU_CONTEXT_USER_SP]
    msr sp_el0, x4
    ldr x3, [x1, #CPU_CONTEXT_STATE]
    tbnz x3, #62, 3f
    tbz x3, #63, 2f

    // Fresh EL0 threads do not return through an exception epilogue, so the
    // saved user register file must be restored here before the first ERET.
    // Without this load, x0-x18 still contain stale EL1 values and new user
    // worker threads start with kernel garbage instead of their bootstrap
    // argument.
    mov x17, x1
    ldr x5, [x1, #CPU_CONTEXT_PC]
    mov x6, #3
    lsl x6, x6, #62
    bic x3, x3, x6
    msr elr_el1, x5
    msr spsr_el1, x3

    ldp x0, x1, [x17, #(0 * 8)]
    ldp x2, x3, [x17, #(2 * 8)]
    ldp x4, x5, [x17, #(4 * 8)]
    ldp x6, x7, [x17, #(6 * 8)]
    ldp x8, x9, [x17, #(8 * 8)]
    ldp x10, x11, [x17, #(10 * 8)]
    ldp x12, x13, [x17, #(12 * 8)]
    ldp x14, x15, [x17, #(14 * 8)]
    ldr x16, [x17, #(16 * 8)]
    ldr x18, [x17, #(18 * 8)]
    ldr x30, [x17, #CPU_CONTEXT_PC]
    ldr x17, [x17, #(17 * 8)]
    eret

2:
    ldr x30, [x1, #CPU_CONTEXT_PC]
    ret

3:
    // Timer IRQ preemption can abandon the original vector call chain entirely.
    // Resume the saved architectural frame directly here so the thread re-enters
    // EL0 without depending on the old kernel stack continuation still being live.
    add x15, x1, #CPU_CONTEXT_EXCEPTION_FRAME
    ldr x5, [x15, #EXC_FRAME_ELR]
    msr elr_el1, x5
    ldr x5, [x15, #EXC_FRAME_SPSR]
    msr spsr_el1, x5
    ldr x2, [x15, #EXC_FRAME_SP]
    mov sp, x2

    ldp x0, x1, [x15, #EXC_FRAME_X0]
    ldp x2, x3, [x15, #EXC_FRAME_X2]
    ldp x4, x5, [x15, #EXC_FRAME_X4]
    ldp x6, x7, [x15, #EXC_FRAME_X6]
    ldp x8, x9, [x15, #EXC_FRAME_X8]
    ldp x10, x11, [x15, #EXC_FRAME_X10]
    ldp x12, x13, [x15, #EXC_FRAME_X12]
    ldp x16, x17, [x15, #EXC_FRAME_X16]
    ldp x18, x19, [x15, #EXC_FRAME_X18]
    ldp x20, x21, [x15, #EXC_FRAME_X20]
    ldp x22, x23, [x15, #EXC_FRAME_X22]
    ldp x24, x25, [x15, #EXC_FRAME_X24]
    ldp x26, x27, [x15, #EXC_FRAME_X26]
    ldp x28, x29, [x15, #EXC_FRAME_X28]
    ldr x30, [x15, #EXC_FRAME_X30]
    ldp x14, x15, [x15, #EXC_FRAME_X14]
    eret

aarch64_thread_entry:
    // Fresh kernel threads start here after the first software context switch.
    // x19 is preloaded with the C entrypoint so the trampoline stays tiny and ABI-stable.
    // New threads must not inherit the scheduler's temporary IRQ mask from the switch path.
    msr daifclr, #2
    blr x19
    bl scheduler_thread_exit_current

1:
    wfi
    b 1b