#ifndef KERNEL_INCLUDE_SCHEDULER_H
#define KERNEL_INCLUDE_SCHEDULER_H

#include "process.h"
#include "thread.h"

enum class WaitReason : U32 {
    None = 0,
    Delay,
    Event,
    Mutex,
    Semaphore,
    Message,
    IO,
};

class Scheduler final {
public:
    /**
     * Reset scheduler-visible state so boot can start from a known empty queue.
     *
     * @return StatusOK when the in-memory scheduler state is ready for bootstrap.
     */
    static Status init(void);

    /**
     * Attach the currently executing boot CPU context to the new process and thread model.
     *
     * This exists so the kernel can transition from early single-threaded startup into
     * scheduler-owned state without attempting an artificial context switch before the
     * low-level handoff code exists.
     *
     * @param process_name Name assigned to the permanent bootstrap kernel process.
     * @param thread_name Name assigned to the permanent bootstrap kernel thread.
     * @return StatusOK on success, or an error if the bootstrap objects cannot be created.
     */
    static Status bootstrap(const char* process_name, const char* thread_name);

    /**
     * Place a managed thread on the scheduler ready path.
     *
     * @param thread Thread that should become current immediately or be queued for later.
     * @return StatusOK on success, or an error when the thread is invalid or already tracked.
     */
    static Status enqueue(Thread* thread);

    /**
     * Park the current thread until another kernel path makes it runnable again.
     *
     * Event-driven user tools should not spin in syscalls while waiting for new
     * work. This helper moves the current thread out of the ready set so the
     * scheduler can run something else until a producer wakes it.
     *
     * @param reason Logical reason for the wait, recorded on the thread while it is parked.
     * @param wait_object Optional object associated with the wait.
     * @return StatusOK after the thread resumes, or an error if the current thread cannot block.
     */
    static Status block_current(WaitReason reason, ObjectHeader* wait_object = NULL);

    /**
     * Park the current thread until either a producer wakes it or the timeout
     * elapses.
     *
     * This keeps waiters off the ready queue while still letting subsystems
     * such as IPC pair a proper wake-and-wait path with periodic deadlines.
     *
     * @param reason Logical reason for the wait.
     * @param delay_ticks Maximum number of scheduler ticks to wait.
     * @param wait_object Optional object associated with the wait.
     * @return StatusOK when another subsystem woke the thread, or StatusBusy
     * when the timeout elapsed before the awaited condition arrived.
     */
    static Status block_current_for(WaitReason reason, U64 delay_ticks, ObjectHeader* wait_object = NULL);

    /**
     * Park the current thread for a fixed number of scheduler ticks.
     *
     * The `virt` board still advances scheduler time from cooperative polling
     * sites, so timed waits must live inside the scheduler itself. A syscall
     * busy loop keeps the thread runnable forever and makes `ps` lie about the
     * task state.
     *
     * @param delay_ticks Number of scheduler ticks to wait before waking.
     * @return StatusOK after the thread wakes, or an error if the current
     * thread cannot sleep.
     */
    static Status sleep_current(U64 delay_ticks);

    /**
    * Yield the current CPU to the scheduler so another ready thread can run.
     *
    * @return Nothing. The scheduler keeps the current thread when no other runnable
    * work exists.
     */
    static void yield(void);

    /**
     * Service the early scheduler clock from normal thread context.
     *
     * This exists because the current `virt` bring-up does not yet wire the architected timer
     * interrupt through a board interrupt controller, so the scheduler must consume elapsed
     * timer quanta from cooperative polling sites.
     *
     * @return Nothing. The scheduler may yield if a prior timer tick expired the current slice.
     */
    static void poll(void);

    /**
     * Reap threads that have already switched away after requesting exit.
     *
     * User threads terminate from syscall context and cannot destroy their own
     * thread object while still executing on that stack. The scheduler parks
     * them on a retired list first, then another safe kernel entry point must
     * finish destruction once the CPU is no longer running on the exiting
     * stack. Exposing this hook lets common kernel entry paths force that
     * cleanup instead of waiting for a lucky later reschedule.
     *
     * @return Nothing.
     */
    static void reap_retired(void);

    /**
     * Remove one thread from scheduler-owned queues before destruction.
     *
     * Timed sleeps and ready-queue membership both keep intrusive links inside
     * `Thread`. Process teardown must clear those links before freeing the
     * object or the next timer poll would chase a dangling pointer.
     *
     * @param thread Thread being detached from scheduler state.
     * @return Nothing.
     */
    static void forget(Thread* thread);

    /**
    * Advance scheduler-owned time accounting from the architected timer source.
    *
    * @return Nothing. The timer path only marks reschedule work and leaves the actual switch to
    * a safe thread-context polling site.
    */
    static void timer_tick(void);

    /**
     * Report the thread that currently owns execution on the boot CPU.
     *
     * @return Current thread pointer, or NULL before bootstrap completes.
     */
    static Thread* current(void);

    /**
     * Report the process that owns the current executing thread.
     *
     * @return Current process pointer, or NULL before bootstrap completes.
     */
    static Process* current_process(void);

    /**
     * Report how many threads are parked in the explicit early ready queue.
     *
     * @return Number of threads waiting behind the current thread.
     */
    static Size ready_count(void);

    /**
     * Report the number of scheduler timer ticks observed since boot.
     *
     * @return Monotonic scheduler tick count.
     */
    static U64 tick_count(void);
};

#endif // KERNEL_INCLUDE_SCHEDULER_H
