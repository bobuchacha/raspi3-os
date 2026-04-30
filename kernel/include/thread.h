/**
 * thread.h
 *
 * gives description of a thread.
 * Note: Please keep all comments clearly
 */
#pragma once

#include "types.h"
#include "object.h"
#include "arch/aarch64/exception_frame.h"

#if !defined(__cplusplus)
#error "thread.h requires C++"
#endif

struct Process;

enum class ThreadState : U32 {
    Initialized = 0,
    Ready,
    Running,
    Waiting,
    Suspended,
    Terminated
};

typedef U64 ThreadId;
inline constexpr U32 ThreadNameCapacity = 32U;
inline constexpr U8 ThreadPriorityHighest = 0U;
inline constexpr U8 ThreadPriorityNormal = 16U;
inline constexpr U8 ThreadPriorityIdle = 31U;
inline constexpr U8 ThreadPriorityLevelCount = 32U;
inline constexpr U32 ThreadDefaultQuantumTicks = 1U;
inline constexpr U64 CpuContextUserModeFlag = 1ULL << 63;
inline constexpr U64 CpuContextResumeExceptionFrameFlag = 1ULL << 62;
inline constexpr U64 CpuContextSchedulerFlagsMask = CpuContextUserModeFlag | CpuContextResumeExceptionFrameFlag;
inline constexpr U64 CpuContextProcessorStateMask = ~CpuContextSchedulerFlagsMask;

struct CpuContext {
    U64 general_registers[31];
    U64 stack_pointer;              // kernel stack pointer
    U64 user_stack_pointer;
    U64 program_counter;
    U64 processor_state;
    AArch64ExceptionFrame saved_exception_frame;
};

struct Thread {
    ObjectHeader header;
    ThreadId id;
    char name[ThreadNameCapacity];
    ThreadState current_state;
    U8 base_priority;
    U8 current_priority;
    U16 reserved0;
    U32 quantum_ticks;
    U32 quantum_ticks_remaining;
    bool in_ready_queue;
    U8 reserved1[7];
    Process* parent;
    VirtAddr user_stack_top;
    void* user_stack_backing;
    Size user_stack_bytes;
    U32 user_stack_slot_index;
    U32 reserved_user_stack0;
    void* kernel_stack_backing;
    VirtAddr kernel_stack_top;
    ObjectHeader* wait_object;
    U32 wait_status;
    // Timed waits store their absolute wake tick here so the cooperative timer
    // can move the thread back to READY without a syscall-side busy loop.
    U64 wake_tick;
    CpuContext context;
    Thread* ready_next;
    Thread* ready_prev;
    // Delayed waits use a dedicated intrusive list so sleeping threads never
    // alias the ready queue bookkeeping while they are parked.
    Thread* sleep_next;
    Thread* sleep_prev;
    Thread* next;
    Thread* prev;
    Thread* process_next;
    Thread* process_prev;
};

class ThreadManager final {
public:
    /**
     * Initialize the fixed bootstrap-safe thread registry.
     *
     * @return StatusOK when thread slots are ready for allocation.
     */
    static Status init(void);

    /**
     * Create a fresh managed thread object for a process.
     *
     * @param parent Process that will own the new thread.
     * @param name Human-readable thread name used for diagnostics.
     * @param entry_point Initial PC for the new thread.
     * @param out_thread Receives the created thread on success.
     * @return StatusOK on success, or an error if allocation or registration fails.
     */
    static Status create_thread(
        Process* parent,
        const char* name,
        VirtAddr entry_point,
        Thread** out_thread,
        U8 base_priority = ThreadPriorityNormal,
        U32 quantum_ticks = ThreadDefaultQuantumTicks);

    /**
     * Create a thread that will enter EL0 on its first dispatch.
     *
     * User threads keep a private kernel exception stack for syscalls/IRQs, but
     * start execution with `SP_EL0` and `ELR_EL1` loaded from this descriptor.
     * The scheduler still owns the thread like any other runnable object.
     *
     * @param parent User process that owns the thread.
     * @param name Human-readable thread name used for diagnostics.
     * @param entry_point Initial EL0 PC for the new thread.
     * @param user_stack_top Initial EL0 SP value for the new thread.
     * @param out_thread Receives the created thread on success.
     * @param base_priority Initial scheduler priority.
     * @param quantum_ticks Scheduler quantum in ticks.
     * @return StatusOK on success, or an error if allocation or registration fails.
     */
    static Status create_user_thread(
        Process* parent,
        const char* name,
        VirtAddr entry_point,
        VirtAddr user_stack_top,
        Thread** out_thread,
        U8 base_priority = ThreadPriorityNormal,
        U32 quantum_ticks = ThreadDefaultQuantumTicks);

    /**
     * Adopt the already-running bootstrap CPU context as a managed thread object.
     *
     * This avoids inventing a fake stack frame for the boot CPU before the scheduler has
     * a real low-level context switch path. The scheduler can then reason about the boot
     * thread using the same data structures as later threads.
     *
     * @param parent Process that should own the live bootstrap thread.
     * @param name Human-readable thread name used for diagnostics.
     * @param out_thread Receives the adopted thread on success.
     * @return StatusOK on success, or an error if the current context cannot be registered.
     */
    static Status adopt_current_thread(Process* parent, const char* name, Thread** out_thread);

    /**
     * Destroy a managed thread object and detach it from all registries.
     *
     * @param thread Thread instance to remove.
     * @return StatusOK on success, or StatusNotFound if the thread is not managed.
     */
    static Status destroy_thread(Thread* thread);

    /**
     * Terminate a thread by identifier.
     *
     * @param thread_id Identifier of the thread to terminate.
     * @return StatusOK on success, or StatusNotFound if the id is unknown.
     */
    static Status terminate_thread(ThreadId thread_id);

    /**
     * Find a managed thread by identifier.
     *
     * @param thread_id Identifier to search for.
     * @return Matching thread pointer, or NULL when no thread owns that id.
     */
    static Thread* find_thread(ThreadId thread_id);

    /**
     * Report how many managed threads currently exist.
     *
     * @return Active thread count.
     */
    static Size thread_count(void);
};