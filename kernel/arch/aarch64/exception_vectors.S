.section .text.vectors, "ax"

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
.equ EXC_FRAME_ESR,     272
.equ EXC_FRAME_FAR,     280
.equ EXC_FRAME_SCTLR,   288
.equ EXC_FRAME_TCR,     296
.equ EXC_FRAME_VECTOR,  304
.equ EXC_FRAME_FLAGS,   312
.equ EXC_FRAME_SIZE,    320
.equ ESR_ELx_EC_SHIFT, 26
.equ ESR_ELx_EC_SVC64, 0x15

.equ VECTOR_CURRENT_SP0_SYNC,    0
.equ VECTOR_CURRENT_SP0_IRQ,     1
.equ VECTOR_CURRENT_SP0_FIQ,     2
.equ VECTOR_CURRENT_SP0_SERROR,  3
.equ VECTOR_CURRENT_SPX_SYNC,    4
.equ VECTOR_CURRENT_SPX_IRQ,     5
.equ VECTOR_CURRENT_SPX_FIQ,     6
.equ VECTOR_CURRENT_SPX_SERROR,  7
.equ VECTOR_LOWER_A64_SYNC,      8
.equ VECTOR_LOWER_A64_IRQ,       9
.equ VECTOR_LOWER_A64_FIQ,       10
.equ VECTOR_LOWER_A64_SERROR,    11
.equ VECTOR_LOWER_A32_SYNC,      12
.equ VECTOR_LOWER_A32_IRQ,       13
.equ VECTOR_LOWER_A32_FIQ,       14
.equ VECTOR_LOWER_A32_SERROR,    15

.macro VECTOR target
    .align 7
    b \target
.endm

.global aarch64_exception_vectors
.extern aarch64_handle_irq
.extern aarch64_exception_debugger
.extern aarch64_capture_current_user_context
.extern aarch64_dispatch_service

.macro SAVE_EXCEPTION_FRAME vector_id
    sub sp, sp, #EXC_FRAME_SIZE

    stp x0, x1, [sp, #EXC_FRAME_X0]
    stp x2, x3, [sp, #EXC_FRAME_X2]
    stp x4, x5, [sp, #EXC_FRAME_X4]
    stp x6, x7, [sp, #EXC_FRAME_X6]
    stp x8, x9, [sp, #EXC_FRAME_X8]
    stp x10, x11, [sp, #EXC_FRAME_X10]
    stp x12, x13, [sp, #EXC_FRAME_X12]
    stp x14, x15, [sp, #EXC_FRAME_X14]
    stp x16, x17, [sp, #EXC_FRAME_X16]
    stp x18, x19, [sp, #EXC_FRAME_X18]
    stp x20, x21, [sp, #EXC_FRAME_X20]
    stp x22, x23, [sp, #EXC_FRAME_X22]
    stp x24, x25, [sp, #EXC_FRAME_X24]
    stp x26, x27, [sp, #EXC_FRAME_X26]
    stp x28, x29, [sp, #EXC_FRAME_X28]
    str x30, [sp, #EXC_FRAME_X30]

    add x9, sp, #EXC_FRAME_SIZE
    str x9, [sp, #EXC_FRAME_SP]

    mrs x9, elr_el1
    str x9, [sp, #EXC_FRAME_ELR]
    mrs x9, spsr_el1
    str x9, [sp, #EXC_FRAME_SPSR]
    mrs x9, esr_el1
    str x9, [sp, #EXC_FRAME_ESR]
    mrs x9, far_el1
    str x9, [sp, #EXC_FRAME_FAR]
    mrs x9, sctlr_el1
    str x9, [sp, #EXC_FRAME_SCTLR]
    mrs x9, tcr_el1
    str x9, [sp, #EXC_FRAME_TCR]

    mov x9, #\vector_id
    str x9, [sp, #EXC_FRAME_VECTOR]
    str xzr, [sp, #EXC_FRAME_FLAGS]
.endm

.macro RESTORE_EXCEPTION_FRAME
    ldr x9, [sp, #EXC_FRAME_ELR]
    msr elr_el1, x9
    ldr x9, [sp, #EXC_FRAME_SPSR]
    msr spsr_el1, x9

    ldr x30, [sp, #EXC_FRAME_X30]
    ldp x28, x29, [sp, #EXC_FRAME_X28]
    ldp x26, x27, [sp, #EXC_FRAME_X26]
    ldp x24, x25, [sp, #EXC_FRAME_X24]
    ldp x22, x23, [sp, #EXC_FRAME_X22]
    ldp x20, x21, [sp, #EXC_FRAME_X20]
    ldp x18, x19, [sp, #EXC_FRAME_X18]
    ldp x16, x17, [sp, #EXC_FRAME_X16]
    ldp x14, x15, [sp, #EXC_FRAME_X14]
    ldp x12, x13, [sp, #EXC_FRAME_X12]
    ldp x10, x11, [sp, #EXC_FRAME_X10]
    ldp x8, x9, [sp, #EXC_FRAME_X8]
    ldp x6, x7, [sp, #EXC_FRAME_X6]
    ldp x4, x5, [sp, #EXC_FRAME_X4]
    ldp x2, x3, [sp, #EXC_FRAME_X2]
    ldp x0, x1, [sp, #EXC_FRAME_X0]

    add sp, sp, #EXC_FRAME_SIZE
.endm

.macro ENTER_DEBUGGER vector_id
    SAVE_EXCEPTION_FRAME \vector_id
    mov x0, sp
    bl aarch64_exception_debugger
    RESTORE_EXCEPTION_FRAME
    eret
.endm

.align 11
aarch64_exception_vectors:
    VECTOR aarch64_current_sp0_sync
    VECTOR aarch64_current_sp0_irq
    VECTOR aarch64_current_sp0_fiq
    VECTOR aarch64_current_sp0_serror

    VECTOR aarch64_current_spx_sync
    VECTOR aarch64_current_spx_irq
    VECTOR aarch64_current_spx_fiq
    VECTOR aarch64_current_spx_serror

    VECTOR aarch64_lower_el_sync
    VECTOR aarch64_lower_el_a64_irq
    VECTOR aarch64_lower_el_a64_fiq
    VECTOR aarch64_lower_el_a64_serror

    VECTOR aarch64_lower_el_a32_sync
    VECTOR aarch64_lower_el_a32_irq
    VECTOR aarch64_lower_el_a32_fiq
    VECTOR aarch64_lower_el_a32_serror

aarch64_current_sp0_sync:
    ENTER_DEBUGGER VECTOR_CURRENT_SP0_SYNC

aarch64_current_sp0_irq:
    SAVE_EXCEPTION_FRAME VECTOR_CURRENT_SP0_IRQ
    b aarch64_irq_common_saved

aarch64_current_sp0_fiq:
    ENTER_DEBUGGER VECTOR_CURRENT_SP0_FIQ

aarch64_current_sp0_serror:
    ENTER_DEBUGGER VECTOR_CURRENT_SP0_SERROR

aarch64_current_spx_sync:
    ENTER_DEBUGGER VECTOR_CURRENT_SPX_SYNC

aarch64_current_spx_irq:
    SAVE_EXCEPTION_FRAME VECTOR_CURRENT_SPX_IRQ
    b aarch64_irq_common_saved

aarch64_current_spx_fiq:
    ENTER_DEBUGGER VECTOR_CURRENT_SPX_FIQ

aarch64_current_spx_serror:
    ENTER_DEBUGGER VECTOR_CURRENT_SPX_SERROR

aarch64_lower_el_a64_irq:
    b aarch64_irq_common

aarch64_lower_el_a64_fiq:
    ENTER_DEBUGGER VECTOR_LOWER_A64_FIQ

aarch64_lower_el_a64_serror:
    ENTER_DEBUGGER VECTOR_LOWER_A64_SERROR

aarch64_lower_el_a32_sync:
    ENTER_DEBUGGER VECTOR_LOWER_A32_SYNC

aarch64_lower_el_a32_irq:
    ENTER_DEBUGGER VECTOR_LOWER_A32_IRQ

aarch64_lower_el_a32_fiq:
    ENTER_DEBUGGER VECTOR_LOWER_A32_FIQ

aarch64_lower_el_a32_serror:
    ENTER_DEBUGGER VECTOR_LOWER_A32_SERROR

aarch64_lower_el_sync:
    SAVE_EXCEPTION_FRAME VECTOR_LOWER_A64_SYNC
    mrs x0, sp_el0
    ldr x1, [sp, #EXC_FRAME_ELR]
    ldr x2, [sp, #EXC_FRAME_SPSR]
    bl aarch64_capture_current_user_context

    ldr x11, [sp, #EXC_FRAME_ESR]
    lsr x12, x11, #ESR_ELx_EC_SHIFT
    cmp x12, #ESR_ELx_EC_SVC64
    b.ne aarch64_lower_el_sync_debug

    // Recover the original EL0 syscall arguments from the saved frame before calling the
    // C dispatcher. ELR/SPSR were already read into scratch registers above, so reusing x0/x1
    // directly here would pass the trap metadata instead of the user pointer arguments.
    ldr x0, [sp, #EXC_FRAME_X8]
    ldp x1, x2, [sp, #EXC_FRAME_X0]
    ldp x3, x4, [sp, #EXC_FRAME_X2]
    ldp x5, x6, [sp, #EXC_FRAME_X4]
    mov x7, sp
    bl aarch64_dispatch_service
    str x0, [sp, #EXC_FRAME_X0]

    RESTORE_EXCEPTION_FRAME
    eret

aarch64_lower_el_sync_debug:
    mov x0, sp
    bl aarch64_exception_debugger
    RESTORE_EXCEPTION_FRAME
    eret

aarch64_irq_common:
    SAVE_EXCEPTION_FRAME VECTOR_LOWER_A64_IRQ

aarch64_irq_common_saved:
    mrs x0, sp_el0
    ldr x1, [sp, #EXC_FRAME_ELR]
    ldr x2, [sp, #EXC_FRAME_SPSR]
    bl aarch64_capture_current_user_context

    bl aarch64_handle_irq

    RESTORE_EXCEPTION_FRAME
    eret
