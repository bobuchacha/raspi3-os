#ifndef KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_DEBUGGER_H
#define KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_DEBUGGER_H

#include "arch/aarch64/exception_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Enter the interactive exception debugger for the captured fault frame.
     *
     * The handler may update ELR/SPSR in place so the assembly epilogue can resume
     * execution directly from the edited state when the operator chooses to
     * continue.
     *
     * @param frame Saved architectural register frame owned by the vector entry.
     */
    void aarch64_exception_debugger(AArch64ExceptionFrame* frame);

    /**
     * Enter the debugger for an operator-requested session on a live trap frame.
     *
     * The debug-shell syscall uses this helper so the debugger can label the
     * session as deliberate instead of presenting it as an unexpected fault.
     *
     * @param frame Saved architectural register frame that will be resumed when
     * the operator continues execution.
     */
    void aarch64_exception_debugger_enter_deliberate(AArch64ExceptionFrame* frame);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
/**
 * Trigger a deliberate architectural breakpoint.
 *
 * The macro is kept in the live tree so ad-hoc kernel debugging can reuse the
 * new exception debugger without reaching back into the legacy headers.
 */
#define breakpoint __asm__ volatile("brk #0")
#endif

#endif // KERNEL_INCLUDE_ARCH_AARCH64_EXCEPTION_DEBUGGER_H