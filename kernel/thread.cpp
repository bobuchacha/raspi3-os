#include "thread.h"

#include "heap.h"
#include "kernel_event_broker.h"
#include "mm.h"
#include "process.h"
#include "resource_manager.h"
#include "scheduler.h"

extern "C" void aarch64_thread_entry(void);

namespace {

    inline constexpr Size KernelThreadStackSize = 16U * 1024U;

    Size g_thread_count;
    ThreadId g_next_thread_id = 1;
    Thread* g_thread_head;
    Thread* g_thread_tail;

    static_assert(offsetof(CpuContext, general_registers) == 0U, "CpuContext register array layout changed");
    static_assert(offsetof(CpuContext, stack_pointer) == (31U * sizeof(U64)), "CpuContext SP offset changed");
    static_assert(offsetof(CpuContext, user_stack_pointer) == (32U * sizeof(U64)), "CpuContext user SP offset changed");
    static_assert(offsetof(CpuContext, program_counter) == (33U * sizeof(U64)), "CpuContext PC offset changed");
    static_assert(offsetof(CpuContext, processor_state) == (34U * sizeof(U64)), "CpuContext PSTATE offset changed");
    static_assert(offsetof(CpuContext, saved_exception_frame) == (35U * sizeof(U64)), "CpuContext saved exception frame offset changed");

    void copy_text(char* destination, Size capacity, const char* source, const char* fallback) {
        Size index = 0;
        const char* active = ((source != NULL) && (source[0] != '\0')) ? source : fallback;

        if ((destination == NULL) || (capacity == 0U)) {
            return;
        }

        while ((index + 1U < capacity) && (active[index] != '\0')) {
            destination[index] = active[index];
            ++index;
        }

        destination[index] = '\0';
    }

    bool contains_thread(const Thread* thread) {
        for (const Thread* current = g_thread_head; current != NULL; current = current->next) {
            if (current == thread) {
                return true;
            }
        }

        return false;
    }

    Thread* allocate_thread_slot(void) {
        return static_cast<Thread*>(Heap::alloc(sizeof(Thread), alignof(Thread)));
    }

    void release_thread_slot(Thread* thread) {
        if (thread == NULL) {
            return;
        }

        memzero(thread, sizeof(*thread));
        Heap::free(thread);
    }

    VirtAddr thread_kernel_stack_top(const Thread* thread) {
        if ((thread == NULL) || (thread->kernel_stack_backing == NULL)) {
            return 0;
        }

        return reinterpret_cast<VirtAddr>(static_cast<U8*>(thread->kernel_stack_backing) + KernelThreadStackSize);
    }

    void link_thread(Thread* thread) {
        // The global thread list is separate from the per-process list so future scheduler code can iterate runnable threads without scanning processes.
        thread->prev = g_thread_tail;
        thread->next = NULL;

        if (g_thread_tail != NULL) {
            g_thread_tail->next = thread;
        }
        else {
            g_thread_head = thread;
        }

        g_thread_tail = thread;
    }

    void unlink_thread(Thread* thread) {
        if (thread->prev != NULL) {
            thread->prev->next = thread->next;
        }
        else {
            g_thread_head = thread->next;
        }

        if (thread->next != NULL) {
            thread->next->prev = thread->prev;
        }
        else {
            g_thread_tail = thread->prev;
        }

        thread->next = NULL;
        thread->prev = NULL;
    }

    VirtAddr current_stack_pointer(void) {
        VirtAddr stack_pointer;

        __asm__ volatile("mov %0, sp" : "=r"(stack_pointer));
        return stack_pointer;
    }

} // namespace

Status ThreadManager::init(void) {
    g_thread_count = 0;
    g_next_thread_id = 1;
    g_thread_head = NULL;
    g_thread_tail = NULL;
    return StatusOK;
}

Status ThreadManager::create_thread(
    Process* parent,
    const char* name,
    VirtAddr entry_point,
    Thread** out_thread,
    U8 base_priority,
    U32 quantum_ticks) {
    Thread* thread;
    Status status;

    if ((parent == NULL) || (out_thread == NULL)) {
        return StatusInvalidArgument;
    }
    if (entry_point == 0U) {
        return StatusInvalidArgument;
    }
    if ((base_priority >= ThreadPriorityLevelCount) || (quantum_ticks == 0U)) {
        return StatusInvalidArgument;
    }
    if (ProcessManager::find_process(parent->id) != parent) {
        return StatusNotFound;
    }

    thread = allocate_thread_slot();
    if (thread == NULL) {
        return StatusNoMemory;
    }

    memzero(thread, sizeof(*thread));
    thread->header.type = ObjectType::Thread;
    thread->header.flags = ObjectFlags::Kernel;
    thread->id = g_next_thread_id++;
    thread->header.object_id = thread->id;
    thread->header.ref_count = 1;
    thread->current_state = ThreadState::Ready;
    thread->base_priority = base_priority;
    thread->current_priority = base_priority;
    thread->quantum_ticks = quantum_ticks;
    thread->quantum_ticks_remaining = quantum_ticks;
    copy_text(thread->name, COUNT_OF(thread->name), name, "thread");
    thread->kernel_stack_backing = Heap::alloc(KernelThreadStackSize, 16U);
    if (thread->kernel_stack_backing == NULL) {
        release_thread_slot(thread);
        return StatusNoMemory;
    }
    thread->kernel_stack_top = thread_kernel_stack_top(thread);

    // Fresh kernel threads start on their private stack inside the assembly trampoline so the
    // first switch lands in the same ABI shape as later voluntary yields.
    thread->context.stack_pointer = thread->kernel_stack_top;
    thread->context.user_stack_pointer = 0U;
    thread->context.program_counter = reinterpret_cast<VirtAddr>(&aarch64_thread_entry);
    thread->context.general_registers[19] = entry_point;

    status = KernelObjectManager::register_object(&thread->header);
    if (status != StatusOK) {
        Heap::free(thread->kernel_stack_backing);
        thread->kernel_stack_backing = NULL;
        release_thread_slot(thread);
        return status;
    }

    link_thread(thread);
    ++g_thread_count;

    status = ProcessManager::attach_thread(parent, thread);
    if (status != StatusOK) {
        unlink_thread(thread);
        --g_thread_count;
        (void)KernelObjectManager::unregister_object(&thread->header);
        Heap::free(thread->kernel_stack_backing);
        thread->kernel_stack_backing = NULL;
        release_thread_slot(thread);
        return status;
    }

    // Publish the created snapshot only after the thread is attached so the parent relationship is already stable.
    KernelEventBroker::publish_thread_event(
        KernelEventTypeThreadCreated,
        thread,
        static_cast<U32>(ThreadState::Initialized),
        static_cast<U32>(thread->current_state),
        StatusOK);
    *out_thread = thread;
    return StatusOK;
}

Status ThreadManager::adopt_current_thread(Process* parent, const char* name, Thread** out_thread) {
    Thread* thread;
    Status status;

    if ((parent == NULL) || (out_thread == NULL)) {
        return StatusInvalidArgument;
    }
    if (ProcessManager::find_process(parent->id) != parent) {
        return StatusNotFound;
    }

    thread = allocate_thread_slot();
    if (thread == NULL) {
        return StatusNoMemory;
    }

    memzero(thread, sizeof(*thread));
    thread->header.type = ObjectType::Thread;
    thread->header.flags = ObjectFlags::Kernel;
    thread->id = g_next_thread_id++;
    thread->header.object_id = thread->id;
    thread->header.ref_count = 1;
    thread->current_state = ThreadState::Running;
    thread->base_priority = ThreadPriorityNormal;
    thread->current_priority = ThreadPriorityNormal;
    thread->quantum_ticks = ThreadDefaultQuantumTicks;
    thread->quantum_ticks_remaining = ThreadDefaultQuantumTicks;
    copy_text(thread->name, COUNT_OF(thread->name), name, "boot");
    thread->kernel_stack_backing = NULL;

    // The bootstrap thread is already executing on the current CPU, so capture the live kernel SP and the return PC instead of fabricating a fresh context.
    thread->kernel_stack_top = current_stack_pointer();
    thread->context.stack_pointer = thread->kernel_stack_top;
    thread->context.user_stack_pointer = 0U;
    thread->context.program_counter = reinterpret_cast<VirtAddr>(__builtin_return_address(0));

    status = KernelObjectManager::register_object(&thread->header);
    if (status != StatusOK) {
        release_thread_slot(thread);
        return status;
    }

    link_thread(thread);
    ++g_thread_count;

    status = ProcessManager::attach_thread(parent, thread);
    if (status != StatusOK) {
        unlink_thread(thread);
        --g_thread_count;
        (void)KernelObjectManager::unregister_object(&thread->header);
        release_thread_slot(thread);
        return status;
    }

    KernelEventBroker::publish_thread_event(
        KernelEventTypeThreadCreated,
        thread,
        static_cast<U32>(ThreadState::Initialized),
        static_cast<U32>(thread->current_state),
        StatusOK);
    *out_thread = thread;
    return StatusOK;
}

Status ThreadManager::create_user_thread(
    Process* parent,
    const char* name,
    VirtAddr entry_point,
    VirtAddr user_stack_top,
    Thread** out_thread,
    U8 base_priority,
    U32 quantum_ticks) {
    Thread* thread;
    Status status;

    if ((parent == NULL) || (out_thread == NULL)) {
        return StatusInvalidArgument;
    }
    // The current userspace loader maps the executable image at VA 0, so an EXE whose chosen
    // entry symbol lands at the start of .text legitimately has entry_point == 0. Rejecting that
    // value turns a valid packed image into a generic shell spawn failure.
    if (user_stack_top == 0U) {
        return StatusInvalidArgument;
    }
    if ((base_priority >= ThreadPriorityLevelCount) || (quantum_ticks == 0U)) {
        return StatusInvalidArgument;
    }
    if (ProcessManager::find_process(parent->id) != parent) {
        return StatusNotFound;
    }

    thread = allocate_thread_slot();
    if (thread == NULL) {
        return StatusNoMemory;
    }

    memzero(thread, sizeof(*thread));
    thread->header.type = ObjectType::Thread;
    thread->header.flags = ObjectFlags::None;
    thread->id = g_next_thread_id++;
    thread->header.object_id = thread->id;
    thread->header.ref_count = 1;
    thread->current_state = ThreadState::Ready;
    thread->base_priority = base_priority;
    thread->current_priority = base_priority;
    thread->quantum_ticks = quantum_ticks;
    thread->quantum_ticks_remaining = quantum_ticks;
    thread->user_stack_top = user_stack_top;
    copy_text(thread->name, COUNT_OF(thread->name), name, "user");
    thread->kernel_stack_backing = Heap::alloc(KernelThreadStackSize, 16U);
    if (thread->kernel_stack_backing == NULL) {
        release_thread_slot(thread);
        return StatusNoMemory;
    }
    thread->kernel_stack_top = thread_kernel_stack_top(thread);

    // EL0 threads always enter through an exception return, but still keep a dedicated kernel
    // stack so syscalls and IRQs land on thread-private EL1 state instead of the bootstrap stack.
    thread->context.stack_pointer = thread->kernel_stack_top;
    thread->context.user_stack_pointer = user_stack_top;
    thread->context.program_counter = entry_point;
    thread->context.processor_state = CpuContextUserModeFlag;

    status = KernelObjectManager::register_object(&thread->header);
    if (status != StatusOK) {
        Heap::free(thread->kernel_stack_backing);
        thread->kernel_stack_backing = NULL;
        release_thread_slot(thread);
        return status;
    }

    link_thread(thread);
    ++g_thread_count;

    status = ProcessManager::attach_thread(parent, thread);
    if (status != StatusOK) {
        unlink_thread(thread);
        --g_thread_count;
        (void)KernelObjectManager::unregister_object(&thread->header);
        Heap::free(thread->kernel_stack_backing);
        thread->kernel_stack_backing = NULL;
        release_thread_slot(thread);
        return status;
    }

    KernelEventBroker::publish_thread_event(
        KernelEventTypeThreadCreated,
        thread,
        static_cast<U32>(ThreadState::Initialized),
        static_cast<U32>(thread->current_state),
        StatusOK);
    *out_thread = thread;
    return StatusOK;
}

Status ThreadManager::destroy_thread(Thread* thread) {
    Process* parent = NULL;
    U32 previous_state;

    if (!contains_thread(thread)) {
        return StatusNotFound;
    }

    previous_state = static_cast<U32>(thread->current_state);
    Scheduler::forget(thread);
    thread->current_state = ThreadState::Terminated;
    // Emit the exit record before detach tears down the owning-process link that subscribers need for correlation.
    KernelEventBroker::publish_thread_event(
        KernelEventTypeThreadExited,
        thread,
        previous_state,
        static_cast<U32>(ThreadState::Terminated),
        StatusOK);

    if (thread->parent != NULL) {
        parent = thread->parent;
        if ((thread->user_stack_backing != NULL)
            && (thread->user_stack_bytes != 0U)
            && (thread->user_stack_top >= thread->user_stack_bytes)) {
            const VirtAddr user_stack_base = thread->user_stack_top - thread->user_stack_bytes;

            (void)mm::MemoryManager::unmap(&thread->parent->process_address_space, user_stack_base, thread->user_stack_bytes);
            KernelResourceManager::release(thread->user_stack_backing);
            thread->user_stack_backing = NULL;
            if (thread->user_stack_slot_index < 32U) {
                thread->parent->user_stack_slot_bitmap &= ~(1U << thread->user_stack_slot_index);
            }
            thread->user_stack_bytes = 0U;
            thread->user_stack_slot_index = 0U;
        }
        Status status = ProcessManager::detach_thread(thread->parent, thread);
        if (status != StatusOK) {
            return status;
        }
    }

    unlink_thread(thread);
    if (g_thread_count != 0U) {
        --g_thread_count;
    }

    (void)KernelObjectManager::unregister_object(&thread->header);
    if (thread->kernel_stack_backing != NULL) {
        Heap::free(thread->kernel_stack_backing);
        thread->kernel_stack_backing = NULL;
    }
    release_thread_slot(thread);

    // Natural last-thread exit owns process teardown, but explicit process
    // destruction already walks every thread itself. Skipping the recursive
    // path while the parent is marked terminated avoids double-destroying the
    // same process from inside its own teardown loop.
    if ((parent != NULL)
        && (parent->thread_count == 0U)
        && (parent->current_state != ProcessState::Terminated)
        && !object_has_flag(parent->header.flags, ObjectFlags::Permanent)) {
        (void)ProcessManager::destroy_process(parent);
    }

    return StatusOK;
}

Status ThreadManager::terminate_thread(ThreadId thread_id) {
    Thread* thread = find_thread(thread_id);

    if (thread == NULL) {
        return StatusNotFound;
    }

    return destroy_thread(thread);
}

Thread* ThreadManager::find_thread(ThreadId thread_id) {
    for (Thread* thread = g_thread_head; thread != NULL; thread = thread->next) {
        if (thread->id == thread_id) {
            return thread;
        }
    }

    return NULL;
}

Size ThreadManager::thread_count(void) {
    return g_thread_count;
}