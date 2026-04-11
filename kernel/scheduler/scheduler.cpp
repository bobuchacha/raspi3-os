#include "scheduler.h"

#include "arch.h"
#include "kernel_event_broker.h"
#include "mm.h"
#include "platform.h"

extern "C" void aarch64_context_switch(CpuContext* previous, const CpuContext* next);

namespace {

    typedef struct ReadyQueue {
        Thread* head;
        Thread* tail;
    } ReadyQueue;

    [[noreturn]] void idle_thread_main(void) {
        for (;;) {
            // The early kernel still lacks a board-neutral wakeup interrupt path on `virt`, so the
            // idle thread stays cooperative and lets the polled timer drive deferred reschedules.
            Scheduler::poll();
            Scheduler::yield();
            __asm__ volatile("nop");
        }
    }

    // The early scheduler keeps a small explicit ready queue until low-level context switching arrives.
    Thread* g_current_thread;
    Process* g_current_process;
    Thread* g_idle_thread;
    ReadyQueue g_ready_queues[ThreadPriorityLevelCount];
    U32 g_ready_bitmap;
    Size g_ready_count;
    U64 g_tick_count;
    Thread* g_sleep_head;
    Thread* g_sleep_tail;
    Thread* g_retired_thread_head;
    Thread* g_retired_thread_tail;
    bool g_reschedule_pending;

    bool thread_is_runnable(const Thread* thread) {
        if (thread == NULL) {
            return false;
        }

        return (thread->current_state != ThreadState::Waiting)
            && (thread->current_state != ThreadState::Suspended)
            && (thread->current_state != ThreadState::Terminated);
    }

    bool thread_can_enter_ready_queue(const Thread* thread) {
        return thread_is_runnable(thread) && (thread != g_idle_thread);
    }

    bool thread_has_valid_priority(const Thread* thread) {
        return (thread != NULL) && (thread->current_priority < ThreadPriorityLevelCount);
    }

    void reset_quantum(Thread* thread) {
        if (thread != NULL) {
            thread->quantum_ticks_remaining = (thread->quantum_ticks == 0U) ? ThreadDefaultQuantumTicks : thread->quantum_ticks;
        }
    }

    bool thread_in_sleep_queue(const Thread* thread) {
        if (thread == NULL) {
            return false;
        }

        return (g_sleep_head == thread) || (thread->sleep_prev != NULL) || (thread->sleep_next != NULL);
    }

    int highest_ready_priority(void) {
        if (g_ready_bitmap == 0U) {
            return -1;
        }

        return __builtin_ctz(g_ready_bitmap);
    }

    ReadyQueue* ready_queue_for_priority(U8 priority) {
        if (priority >= ThreadPriorityLevelCount) {
            return NULL;
        }

        return &g_ready_queues[priority];
    }

    Status ready_queue_push(Thread* thread, bool at_tail) {
        ReadyQueue* queue;

        if (!thread_can_enter_ready_queue(thread) || !thread_has_valid_priority(thread)) {
            return StatusInvalidArgument;
        }
        if (thread->in_ready_queue) {
            return StatusAlreadyExists;
        }

        queue = ready_queue_for_priority(thread->current_priority);
        if (queue == NULL) {
            return StatusInvalidArgument;
        }

        thread->ready_prev = NULL;
        thread->ready_next = NULL;
        if (queue->head == NULL) {
            queue->head = thread;
            queue->tail = thread;
        }
        else if (at_tail) {
            thread->ready_prev = queue->tail;
            queue->tail->ready_next = thread;
            queue->tail = thread;
        }
        else {
            thread->ready_next = queue->head;
            queue->head->ready_prev = thread;
            queue->head = thread;
        }

        thread->in_ready_queue = true;
        g_ready_bitmap |= (1U << thread->current_priority);
        ++g_ready_count;
        return StatusOK;
    }

    void ready_queue_remove(Thread* thread) {
        ReadyQueue* queue;

        if ((thread == NULL) || !thread->in_ready_queue || !thread_has_valid_priority(thread)) {
            return;
        }

        queue = ready_queue_for_priority(thread->current_priority);
        if (queue == NULL) {
            thread->in_ready_queue = false;
            thread->ready_prev = NULL;
            thread->ready_next = NULL;
            return;
        }

        if (thread->ready_prev != NULL) {
            thread->ready_prev->ready_next = thread->ready_next;
        }
        else {
            queue->head = thread->ready_next;
        }

        if (thread->ready_next != NULL) {
            thread->ready_next->ready_prev = thread->ready_prev;
        }
        else {
            queue->tail = thread->ready_prev;
        }

        thread->ready_prev = NULL;
        thread->ready_next = NULL;
        thread->in_ready_queue = false;
        if (g_ready_count != 0U) {
            --g_ready_count;
        }
        if (queue->head == NULL) {
            g_ready_bitmap &= ~(1U << thread->current_priority);
        }
    }

    void sleep_queue_remove(Thread* thread) {
        if ((thread == NULL) || !thread_in_sleep_queue(thread)) {
            return;
        }

        if (thread->sleep_prev != NULL) {
            thread->sleep_prev->sleep_next = thread->sleep_next;
        }
        else {
            g_sleep_head = thread->sleep_next;
        }

        if (thread->sleep_next != NULL) {
            thread->sleep_next->sleep_prev = thread->sleep_prev;
        }
        else {
            g_sleep_tail = thread->sleep_prev;
        }

        thread->sleep_prev = NULL;
        thread->sleep_next = NULL;
    }

    void sleep_queue_insert_sorted(Thread* thread) {
        Thread* cursor;

        if (thread == NULL) {
            return;
        }

        sleep_queue_remove(thread);
        thread->sleep_prev = NULL;
        thread->sleep_next = NULL;
        if (g_sleep_head == NULL) {
            g_sleep_head = thread;
            g_sleep_tail = thread;
            return;
        }

        cursor = g_sleep_head;
        while ((cursor != NULL) && (cursor->wake_tick <= thread->wake_tick)) {
            cursor = cursor->sleep_next;
        }

        if (cursor == NULL) {
            thread->sleep_prev = g_sleep_tail;
            g_sleep_tail->sleep_next = thread;
            g_sleep_tail = thread;
            return;
        }

        thread->sleep_next = cursor;
        thread->sleep_prev = cursor->sleep_prev;
        if (cursor->sleep_prev != NULL) {
            cursor->sleep_prev->sleep_next = thread;
        }
        else {
            g_sleep_head = thread;
        }

        cursor->sleep_prev = thread;
    }

    Status transition_thread_to_ready(Thread* thread, bool at_tail) {
        U32 previous_state;
        Status status;

        if (thread == NULL) {
            return StatusInvalidArgument;
        }
        if (thread->current_state == ThreadState::Terminated) {
            return StatusBusy;
        }
        if (thread->in_ready_queue) {
            return StatusAlreadyExists;
        }

        sleep_queue_remove(thread);
        thread->wait_object = NULL;
        thread->wait_status = static_cast<U32>(WaitReason::None);
        thread->wake_tick = 0U;

        if ((thread->parent != NULL)
            && (thread->parent->current_state != ProcessState::Exiting)
            && (thread->parent->current_state != ProcessState::Terminated)) {
            thread->parent->current_state = ProcessState::Running;
        }

        previous_state = static_cast<U32>(thread->current_state);
        thread->current_state = ThreadState::Ready;
        if (thread->quantum_ticks_remaining == 0U) {
            reset_quantum(thread);
        }

        status = ready_queue_push(thread, at_tail);
        if ((status == StatusOK) && (previous_state != static_cast<U32>(ThreadState::Ready))) {
            KernelEventBroker::publish_thread_event(
                KernelEventTypeThreadReady,
                thread,
                previous_state,
                static_cast<U32>(ThreadState::Ready),
                StatusOK);
        }
        if ((status == StatusOK) && (g_current_thread == g_idle_thread)) {
            g_reschedule_pending = true;
        }
        if ((status == StatusOK) && (g_current_thread != NULL) && (g_current_thread != g_idle_thread)
            && (thread->current_priority < g_current_thread->current_priority)) {
            g_reschedule_pending = true;
        }

        return status;
    }

    void wake_expired_sleepers(void) {
        while ((g_sleep_head != NULL) && (g_sleep_head->wake_tick <= g_tick_count)) {
            Thread* thread = g_sleep_head;

            sleep_queue_remove(thread);
            if (thread->current_state == ThreadState::Waiting) {
                (void)transition_thread_to_ready(thread, true);
            }
        }
    }

    Thread* ready_queue_pop_best(void) {
        while (g_ready_bitmap != 0U) {
            int priority = highest_ready_priority();
            ReadyQueue* queue;
            Thread* thread;

            if (priority < 0) {
                return NULL;
            }

            queue = ready_queue_for_priority(static_cast<U8>(priority));
            if ((queue == NULL) || (queue->head == NULL)) {
                g_ready_bitmap &= ~(1U << priority);
                continue;
            }

            thread = queue->head;
            ready_queue_remove(thread);
            if (thread_can_enter_ready_queue(thread) && (thread->current_state == ThreadState::Ready)) {
                return thread;
            }
        }

        return NULL;
    }

    void queue_retired_thread(Thread* thread) {
        if (thread == NULL) {
            return;
        }

        thread->ready_next = NULL;
        if (g_retired_thread_tail != NULL) {
            g_retired_thread_tail->ready_next = thread;
        }
        else {
            g_retired_thread_head = thread;
        }

        g_retired_thread_tail = thread;
    }

    void reap_retired_threads(void) {
        while ((g_retired_thread_head != NULL) && (g_retired_thread_head != g_current_thread)) {
            Thread* retired = g_retired_thread_head;

            g_retired_thread_head = retired->ready_next;
            if (g_retired_thread_head == NULL) {
                g_retired_thread_tail = NULL;
            }

            retired->ready_next = NULL;
            (void)ThreadManager::destroy_thread(retired);
        }
    }

    Status bind_current_thread(Thread* thread) {
        U32 previous_state;
        Status status;

        if (thread == NULL) {
            return StatusInvalidArgument;
        }

        previous_state = static_cast<U32>(thread->current_state);

        if (thread->parent != NULL) {
            // The scheduler switches the process address space here so every future scheduler path can assume
            // the current thread and the active MMU view stay in lockstep, even during the bootstrap-only phase.
            status = mm::MemoryManager::switch_to(&thread->parent->process_address_space);
            if (status != StatusOK) {
                return status;
            }

            thread->parent->current_state = ProcessState::Running;
        }

        thread->current_state = ThreadState::Running;
        if (previous_state != static_cast<U32>(ThreadState::Running)) {
            KernelEventBroker::publish_thread_event(
                KernelEventTypeThreadRunning,
                thread,
                previous_state,
                static_cast<U32>(ThreadState::Running),
                StatusOK);
        }
        g_current_thread = thread;
        g_current_process = thread->parent;
        reset_quantum(thread);
        return StatusOK;
    }

    Thread* pick_next_thread(bool keep_current_if_runnable) {
        Thread* next = ready_queue_pop_best();

        if (next != NULL) {
            return next;
        }
        if (keep_current_if_runnable && thread_is_runnable(g_current_thread) && (g_current_thread != g_idle_thread)) {
            return g_current_thread;
        }
        if (g_idle_thread != NULL) {
            return g_idle_thread;
        }

        return g_current_thread;
    }

    Status switch_to_thread(Thread* next) {
        Thread* previous = g_current_thread;
        Status status;

        if (next == NULL) {
            return StatusNotFound;
        }
        if (next == previous) {
            next->current_state = ThreadState::Running;
            reset_quantum(next);
            return StatusOK;
        }

        status = bind_current_thread(next);
        if (status != StatusOK) {
            return status;
        }

        aarch64_context_switch(previous != NULL ? &previous->context : NULL, &next->context);
        reap_retired_threads();
        return StatusOK;
    }

    Status dispatch(bool requeue_current, bool requeue_at_tail, bool reset_requeued_quantum, bool keep_current_if_runnable) {
        Thread* previous = g_current_thread;

        if ((previous != NULL) && requeue_current && (previous != g_idle_thread) && (previous->current_state == ThreadState::Running)) {
            U32 previous_state = static_cast<U32>(previous->current_state);

            previous->current_state = ThreadState::Ready;
            if (reset_requeued_quantum) {
                reset_quantum(previous);
            }
            if (ready_queue_push(previous, requeue_at_tail) == StatusOK) {
                KernelEventBroker::publish_thread_event(
                    KernelEventTypeThreadReady,
                    previous,
                    previous_state,
                    static_cast<U32>(ThreadState::Ready),
                    StatusOK);
            }
        }

        return switch_to_thread(pick_next_thread(keep_current_if_runnable));
    }

    void process_pending_reschedule(void) {
        int best_priority;

        if (g_current_thread == NULL) {
            if (g_ready_count != 0U) {
                (void)switch_to_thread(pick_next_thread(false));
            }
            return;
        }
        if (g_current_thread == g_idle_thread) {
            if (g_ready_count != 0U) {
                (void)switch_to_thread(pick_next_thread(false));
            }
            return;
        }

        best_priority = highest_ready_priority();
        if ((best_priority >= 0) && (static_cast<U8>(best_priority) < g_current_thread->current_priority)) {
            (void)dispatch(true, false, false, false);
            return;
        }
        if ((g_current_thread->quantum_ticks_remaining == 0U) && (g_ready_count != 0U)) {
            (void)dispatch(true, true, true, false);
            return;
        }
        if (g_current_thread->quantum_ticks_remaining == 0U) {
            reset_quantum(g_current_thread);
        }
    }

    [[noreturn]] void exit_current_thread(void) {
        bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();
        Thread* current = g_current_thread;

        if ((current == NULL) || (current == g_idle_thread)) {
            arch::Arch::halt();
        }

        current->current_state = ThreadState::Terminated;
        queue_retired_thread(current);

        if (switch_to_thread(pick_next_thread(false)) != StatusOK) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            arch::Arch::halt();
        }

        arch::Arch::restore_interrupts(interrupts_enabled);
        arch::Arch::halt();
    }

} // namespace

extern "C" void aarch64_capture_current_user_context(U64 user_stack_pointer, U64 user_program_counter, U64 user_processor_state) {
    Thread* current = g_current_thread;

    if (current == NULL) {
        return;
    }

    // Once an EL0 thread traps into EL1, any later context switch must resume
    // the in-progress kernel frame first so the exception epilogue can restore
    // the stacked user registers. Leaving the user-entry flag set would make the
    // low-level switch path `eret` directly to a kernel return address.
    current->context.user_stack_pointer = user_stack_pointer;
    current->context.program_counter = user_program_counter;
    current->context.processor_state = user_processor_state & CpuContextProcessorStateMask;
}

Status Scheduler::init(void) {
    g_current_thread = NULL;
    g_current_process = NULL;
    g_idle_thread = NULL;
    memzero(g_ready_queues, sizeof(g_ready_queues));
    g_ready_bitmap = 0U;
    g_ready_count = 0;
    g_tick_count = 0;
    g_sleep_head = NULL;
    g_sleep_tail = NULL;
    g_retired_thread_head = NULL;
    g_retired_thread_tail = NULL;
    g_reschedule_pending = false;
    return StatusOK;
}

Status Scheduler::bootstrap(const char* process_name, const char* thread_name) {
    Thread* idle_thread;
    Process* process;
    Thread* thread;
    Status status;

    if (g_current_thread != NULL) {
        return StatusAlreadyExists;
    }

    status = ProcessManager::create_process(process_name, &process);
    if (status != StatusOK) {
        return status;
    }

    // The bootstrap process and thread must stay resident for the entire kernel lifetime because the early
    // scheduler has no idle-thread handoff or teardown path yet.
    process->header.flags = process->header.flags | ObjectFlags::Permanent | ObjectFlags::Kernel;
    process->current_state = ProcessState::Running;

    status = ThreadManager::adopt_current_thread(process, thread_name, &thread);
    if (status != StatusOK) {
        (void)ProcessManager::destroy_process(process);
        return status;
    }

    thread->header.flags = thread->header.flags | ObjectFlags::Permanent | ObjectFlags::Kernel;
    status = ThreadManager::create_thread(
        process,
        "idle",
        reinterpret_cast<VirtAddr>(&idle_thread_main),
        &idle_thread,
        ThreadPriorityIdle,
        ThreadDefaultQuantumTicks);
    if (status != StatusOK) {
        (void)ProcessManager::destroy_process(process);
        return status;
    }

    idle_thread->header.flags = idle_thread->header.flags | ObjectFlags::Permanent | ObjectFlags::Kernel;
    g_idle_thread = idle_thread;
    return bind_current_thread(thread);
}

Status Scheduler::enqueue(Thread* thread) {
    bool interrupts_enabled;
    Status status;

    interrupts_enabled = arch::Arch::save_and_disable_interrupts();
    reap_retired_threads();

    if (thread == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusInvalidArgument;
    }
    if (ThreadManager::find_thread(thread->id) != thread) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusNotFound;
    }
    if (thread->current_state == ThreadState::Terminated) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }
    if (thread == g_current_thread) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusAlreadyExists;
    }
    if (g_current_thread == NULL) {
        status = bind_current_thread(thread);
        arch::Arch::restore_interrupts(interrupts_enabled);
        return status;
    }

    status = transition_thread_to_ready(thread, true);
    arch::Arch::restore_interrupts(interrupts_enabled);
    return status;
}

Status Scheduler::block_current(WaitReason reason, ObjectHeader* wait_object) {
    bool interrupts_enabled;
    Thread* current;
    Status status;

    interrupts_enabled = arch::Arch::save_and_disable_interrupts();
    reap_retired_threads();

    current = g_current_thread;
    if ((current == NULL) || (current == g_idle_thread)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }
    if (current->current_state != ThreadState::Running) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }

    current->wait_object = wait_object;
    current->wait_status = static_cast<U32>(reason);
    current->current_state = ThreadState::Waiting;

    status = switch_to_thread(pick_next_thread(false));
    current = g_current_thread;
    if (current != NULL) {
        current->wait_object = NULL;
        current->wait_status = 0U;
    }
    if (status != StatusOK) {
        if (g_current_thread != NULL) {
            g_current_thread->current_state = ThreadState::Running;
        }
        arch::Arch::restore_interrupts(interrupts_enabled);
        return status;
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
    return StatusOK;
}

Status Scheduler::block_current_for(WaitReason reason, U64 delay_ticks, ObjectHeader* wait_object) {
    bool interrupts_enabled;
    Thread* current;
    Status status;
    bool timed_out = false;

    if (delay_ticks == 0U) {
        return block_current(reason, wait_object);
    }

    interrupts_enabled = arch::Arch::save_and_disable_interrupts();
    reap_retired_threads();

    current = g_current_thread;
    if ((current == NULL) || (current == g_idle_thread)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }
    if (current->current_state != ThreadState::Running) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }

    current->wait_object = wait_object;
    current->wait_status = static_cast<U32>(reason);
    current->wake_tick = g_tick_count + delay_ticks;
    current->current_state = ThreadState::Waiting;
    sleep_queue_insert_sorted(current);

    status = switch_to_thread(pick_next_thread(false));
    current = g_current_thread;
    if (current != NULL) {
        timed_out = (current->wait_status == static_cast<U32>(reason)) && (current->wake_tick != 0U);
        current->wait_object = NULL;
        current->wait_status = static_cast<U32>(WaitReason::None);
        current->wake_tick = 0U;
    }
    if (status != StatusOK) {
        if (current != NULL) {
            sleep_queue_remove(current);
            current->current_state = ThreadState::Running;
        }
        arch::Arch::restore_interrupts(interrupts_enabled);
        return status;
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
    return timed_out ? StatusBusy : StatusOK;
}

Status Scheduler::sleep_current(U64 delay_ticks) {
    bool interrupts_enabled;
    Thread* current;
    Status status;

    if (delay_ticks == 0U) {
        return StatusOK;
    }

    interrupts_enabled = arch::Arch::save_and_disable_interrupts();
    reap_retired_threads();

    current = g_current_thread;
    if ((current == NULL) || (current == g_idle_thread)) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }
    if (current->current_state != ThreadState::Running) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return StatusBusy;
    }

    current->wait_object = NULL;
    current->wait_status = static_cast<U32>(WaitReason::Delay);
    current->wake_tick = g_tick_count + delay_ticks;
    current->current_state = ThreadState::Waiting;
    sleep_queue_insert_sorted(current);

    status = switch_to_thread(pick_next_thread(false));
    current = g_current_thread;
    if (current != NULL) {
        current->wait_object = NULL;
        current->wait_status = static_cast<U32>(WaitReason::None);
        current->wake_tick = 0U;
    }
    if (status != StatusOK) {
        if (current != NULL) {
            sleep_queue_remove(current);
            current->current_state = ThreadState::Running;
        }
        arch::Arch::restore_interrupts(interrupts_enabled);
        return status;
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
    return StatusOK;
}

void Scheduler::yield(void) {
    bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    reap_retired_threads();
    if (g_current_thread == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }
    if ((g_current_thread != g_idle_thread) && (g_ready_count == 0U)) {
        reset_quantum(g_current_thread);
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }

    (void)dispatch(g_current_thread != g_idle_thread, true, true, true);
    arch::Arch::restore_interrupts(interrupts_enabled);
}

void Scheduler::poll(void) {
    // The `virt` board still depends on cooperative polling sites after early bring-up.
    // Timer IRQ delivery is not the only place that must drain device state, otherwise
    // input can stall once execution spends most of its time in scheduler/user loops.
    board::Platform::poll_devices();

    U32 elapsed_ticks = arch::Arch::poll_periodic_timer();

    while (elapsed_ticks != 0U) {
        timer_tick();
        --elapsed_ticks;
    }

    {
        bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

        if (!g_reschedule_pending) {
            arch::Arch::restore_interrupts(interrupts_enabled);
            return;
        }

        g_reschedule_pending = false;
        process_pending_reschedule();
        arch::Arch::restore_interrupts(interrupts_enabled);
    }
}

void Scheduler::reap_retired(void) {
    bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    // Syscall-heavy user workloads can exit without immediately hitting a
    // scheduler path that returns through the C switch helper, so provide an
    // explicit collection point any kernel entry can use to finish teardown.
    reap_retired_threads();
    arch::Arch::restore_interrupts(interrupts_enabled);
}

void Scheduler::forget(Thread* thread) {
    bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    if (thread != NULL) {
        if (thread->in_ready_queue) {
            ready_queue_remove(thread);
        }
        sleep_queue_remove(thread);
        if (g_current_thread == thread) {
            g_current_thread = NULL;
            g_current_process = NULL;
        }
        thread->wait_object = NULL;
        thread->wait_status = static_cast<U32>(WaitReason::None);
        thread->wake_tick = 0U;
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
}

void Scheduler::timer_tick(void) {
    bool interrupts_enabled = arch::Arch::save_and_disable_interrupts();

    ++g_tick_count;
    reap_retired_threads();
    wake_expired_sleepers();

    if (g_current_thread == NULL) {
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }
    if (g_current_thread == g_idle_thread) {
        if (g_ready_count != 0U) {
            g_reschedule_pending = true;
        }
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }
    if (g_ready_count == 0U) {
        reset_quantum(g_current_thread);
        arch::Arch::restore_interrupts(interrupts_enabled);
        return;
    }
    if (g_current_thread->quantum_ticks_remaining > 0U) {
        --g_current_thread->quantum_ticks_remaining;
    }
    if (g_current_thread->quantum_ticks_remaining == 0U) {
        g_reschedule_pending = true;
    }

    arch::Arch::restore_interrupts(interrupts_enabled);
}

Thread* Scheduler::current(void) {
    return g_current_thread;
}

Process* Scheduler::current_process(void) {
    if ((g_current_thread != NULL) && (g_current_thread->parent != NULL)) {
        // The running thread is the source of truth. Early bootstrap and exception-return
        // paths can observe a stale cached process pointer, but the thread ownership must
        // stay accurate for scheduling and teardown to work at all.
        g_current_process = g_current_thread->parent;
    }

    return g_current_process;
}

Size Scheduler::ready_count(void) {
    return g_ready_count;
}

U64 Scheduler::tick_count(void) {
    return g_tick_count;
}

extern "C" [[noreturn]] void scheduler_thread_exit_current(void) {
    exit_current_thread();
}