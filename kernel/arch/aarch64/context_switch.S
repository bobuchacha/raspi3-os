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
    tbz x3, #63, 2f

    ldr x5, [x1, #CPU_CONTEXT_PC]
    mov x6, #1
    lsl x6, x6, #63
    bic x3, x3, x6
    msr elr_el1, x5
    msr spsr_el1, x3
    eret

2:
    ldr x30, [x1, #CPU_CONTEXT_PC]
    ret

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