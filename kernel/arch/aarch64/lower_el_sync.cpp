#include "arch/aarch64/exception_frame.h"

#include "debug-message.h"
#include "mm.h"
#include "process.h"
#include "scheduler.h"
#include "user_heap.h"

extern "C" [[noreturn]] void scheduler_thread_exit_current(void);

namespace {

    inline constexpr U64 LowerElExceptionClassShift = 26U;
    inline constexpr U64 LowerElExceptionClassMask = 0x3fU;
    inline constexpr U64 LowerElInstructionAbortClass = 0x20U;
    inline constexpr U64 LowerElDataAbortClass = 0x24U;
    inline constexpr U64 DataAbortStatusCodeMask = 0x3fU;

    /**
     * Extract the ESR exception class from one saved lower-EL frame.
     *
     * The assembly vector already filtered for the lower-EL sync slot, but the
     * C++ dispatcher re-checks the class so future callers can reuse the helper
     * without depending on the exact vector-assembly branch ordering.
     *
     * @param frame Saved exception frame captured by the vector entry.
     * @return ESR exception class field, or zero when the frame is null.
     */
    U64 exception_class(const AArch64ExceptionFrame* frame) {
        if (frame == NULL) {
            return 0U;
        }

        return (frame->esr >> LowerElExceptionClassShift) & LowerElExceptionClassMask;
    }

    /**
     * Describe one lower-EL instruction or data abort fault status code.
     *
     * Both instruction and data aborts encode their FSC in the same ESR bits,
     * so one shared decoder keeps the serial fault log consistent across the
     * user-mode crash cases that should terminate the process instead of the
     * whole kernel session.
     *
     * @param esr Raw ESR_EL1 value captured in the exception frame.
     * @return Short static text describing the abort class.
     */
    const char* abort_reason(U64 esr) {
        switch (esr & DataAbortStatusCodeMask) {
        case 0x04U:
        case 0x05U:
        case 0x06U:
        case 0x07U:
            return "translation";
        case 0x09U:
        case 0x0aU:
        case 0x0bU:
            return "access-flag";
        case 0x0dU:
        case 0x0eU:
        case 0x0fU:
            return "permission";
        case 0x10U:
        case 0x11U:
            return "external-abort";
        default:
            return "other";
        }
    }

    /**
     * Return a human-readable name for one lower-EL abort class.
     *
     * The user-fault path now handles both instruction and data aborts. Naming
     * the class in one helper keeps the emitted serial diagnostics stable.
     *
     * @param exception_class_value ESR exception class extracted from the frame.
     * @return Static class name.
     */
    const char* abort_class_name(U64 exception_class_value) {
        if (exception_class_value == LowerElInstructionAbortClass) {
            return "instruction";
        }
        if (exception_class_value == LowerElDataAbortClass) {
            return "data";
        }

        return "sync";
    }

    /**
     * Report whether the current exception owner is a user process.
     *
     * Fatal EL0 abort recovery should never try to tear down kernel-owned or
     * permanent bootstrap processes. Returning false here leaves those cases on
     * the debugger path where the fault remains fully inspectable.
     *
     * @param process Scheduler-owned current process pointer.
     * @return True when the current process is a normal user-mode owner.
     */
    bool is_user_process_fault_target(const Process* process) {
        if (process == NULL) {
            return false;
        }

        return !object_has_flag(process->header.flags, ObjectFlags::Kernel)
            && !object_has_flag(process->header.flags, ObjectFlags::Permanent);
    }

} // namespace

extern "C" U64 aarch64_handle_lower_el_sync(AArch64ExceptionFrame* frame) {
    Process* current_process;
    const U64 fault_class = exception_class(frame);
    const U64 exit_code = static_cast<U64>(static_cast<I64>(StatusFault));

    if ((frame == NULL)
        || ((fault_class != LowerElInstructionAbortClass)
            && (fault_class != LowerElDataAbortClass))) {
        return 0U;
    }

    current_process = Scheduler::current_process();
    if (!is_user_process_fault_target(current_process)) {
        return 0U;
    }

    if ((fault_class == LowerElDataAbortClass)
        && (UserHeap::handle_page_fault(current_process, frame->far, frame->esr) == StatusOK)) {
        return 1U;
    }

    KERROR(
        "[fault] fatal user %s abort pid=%llu name=%s esr=0x%llx far=0x%llx elr=0x%llx sp=0x%llx reason=%s\n",
        abort_class_name(fault_class),
        static_cast<unsigned long long>(current_process->id),
        current_process->name,
        static_cast<unsigned long long>(frame->esr),
        static_cast<unsigned long long>(frame->far),
        static_cast<unsigned long long>(frame->elr),
        static_cast<unsigned long long>(frame->stack_pointer),
        abort_reason(frame->esr));

    // Collapse the launched subtree first so background children do not keep
    // running after the root app dies from a fatal EL0 instruction/data fault.
    (void)ProcessManager::terminate_process_tree(current_process->id, exit_code, current_process->id);

    // Mark the current process as exiting before retiring the current thread so
    // later safe kernel entry points can reap the root process itself.
    current_process->exit_code = exit_code;
    current_process->current_state = ProcessState::Exiting;
    scheduler_thread_exit_current();
}