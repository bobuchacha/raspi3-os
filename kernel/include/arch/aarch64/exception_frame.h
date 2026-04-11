#ifndef KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_FRAME_H
#define KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_FRAME_H

#include "types.h"

/**
 * AArch64ExceptionVector
 *
 * Identifies which architectural vector entry captured the frame so the
 * debugger can explain whether the fault came from the kernel, userspace,
 * or an unexpected IRQ/FIQ/error slot.
 */
typedef enum AArch64ExceptionVector {
    AArch64ExceptionVectorCurrentSp0Sync = 0,
    AArch64ExceptionVectorCurrentSp0Irq = 1,
    AArch64ExceptionVectorCurrentSp0Fiq = 2,
    AArch64ExceptionVectorCurrentSp0SError = 3,
    AArch64ExceptionVectorCurrentSpXSync = 4,
    AArch64ExceptionVectorCurrentSpXIrq = 5,
    AArch64ExceptionVectorCurrentSpXFiq = 6,
    AArch64ExceptionVectorCurrentSpXSError = 7,
    AArch64ExceptionVectorLowerElA64Sync = 8,
    AArch64ExceptionVectorLowerElA64Irq = 9,
    AArch64ExceptionVectorLowerElA64Fiq = 10,
    AArch64ExceptionVectorLowerElA64SError = 11,
    AArch64ExceptionVectorLowerElA32Sync = 12,
    AArch64ExceptionVectorLowerElA32Irq = 13,
    AArch64ExceptionVectorLowerElA32Fiq = 14,
    AArch64ExceptionVectorLowerElA32SError = 15,
} AArch64ExceptionVector;

/**
 * Reserved debugger-entry flags stored in AArch64ExceptionFrame::reserved0.
 *
 * The vector code clears this word on every exception so higher-level code can
 * annotate deliberate debugger entries without changing the architectural save
 * layout that the assembly epilogue restores.
 */
#define AARCH64_EXCEPTION_FRAME_FLAG_DELIBERATE_ENTRY (1ULL << 0)

/**
 * AArch64ExceptionFrame
 *
 * This is the exact register image captured by the exception vectors before
 * entering C++. Keeping the layout stable lets the debugger inspect and edit
 * ELR/SPSR in place, and lets assembly restore execution without any
 * translation layer.
 */
typedef struct AArch64ExceptionFrame {
    U64 general_registers[31];
    U64 stack_pointer;
    U64 elr;
    U64 spsr;
    U64 esr;
    U64 far;
    U64 sctlr;
    U64 tcr;
    U64 vector_id;
    U64 reserved0;
} AArch64ExceptionFrame;

#if defined(__cplusplus)
static_assert(offsetof(AArch64ExceptionFrame, general_registers) == 0U, "Exception frame GPR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, stack_pointer) == (31U * sizeof(U64)), "Exception frame SP offset changed");
static_assert(offsetof(AArch64ExceptionFrame, elr) == (32U * sizeof(U64)), "Exception frame ELR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, spsr) == (33U * sizeof(U64)), "Exception frame SPSR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, esr) == (34U * sizeof(U64)), "Exception frame ESR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, far) == (35U * sizeof(U64)), "Exception frame FAR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, sctlr) == (36U * sizeof(U64)), "Exception frame SCTLR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, tcr) == (37U * sizeof(U64)), "Exception frame TCR offset changed");
static_assert(offsetof(AArch64ExceptionFrame, vector_id) == (38U * sizeof(U64)), "Exception frame vector offset changed");
static_assert(sizeof(AArch64ExceptionFrame) == (40U * sizeof(U64)), "Exception frame size changed");
#endif

#endif // KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_FRAME_H