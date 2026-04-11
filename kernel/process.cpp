#include "process.h"

#include "gui_service.h"
#include "heap.h"
#include "heap_list.h"
#include "kernel_event_broker.h"
#include "loader.h"
#include "mm.h"
#include "scheduler.h"
#include "shared_memory.h"
#include "thread.h"
#include "user_heap.h"

namespace {

    typedef struct ProcessExitRecord {
        Pid process_id;
        Pid parent_process_id;
        U64 exit_code;
    } ProcessExitRecord;

    HeapList<ProcessExitRecord> exit_records;
    Size process_count_value;
    Pid next_pid = 1;
    Process* process_head;
    Process* process_tail;

    bool same_text(const char* lhs, const char* rhs) {
        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((*lhs != '\0') && (*rhs != '\0')) {
            if (*lhs != *rhs) {
                return false;
            }
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }

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

    bool contains_process(const Process* process) {
        for (const Process* current = process_head; current != NULL; current = current->next) {
            if (current == process) {
                return true;
            }
        }

        return false;
    }

    Process* allocate_process_slot(void) {
        return static_cast<Process*>(Heap::alloc(sizeof(Process), alignof(Process)));
    }

    void release_process_slot(Process* process) {
        if (process == NULL) {
            return;
        }

        memzero(process, sizeof(*process));
        Heap::free(process);
    }

    void link_process(Process* process) {
        // The intrusive list gives O(1) insertion/removal without any allocator involvement.
        process->prev = process_tail;
        process->next = NULL;

        if (process_tail != NULL) {
            process_tail->next = process;
        }
        else {
            process_head = process;
        }

        process_tail = process;
    }

    void unlink_process(Process* process) {
        if (process->prev != NULL) {
            process->prev->next = process->next;
        }
        else {
            process_head = process->next;
        }

        if (process->next != NULL) {
            process->next->prev = process->prev;
        }
        else {
            process_tail = process->prev;
        }

        process->next = NULL;
        process->prev = NULL;
    }

    void remember_exit_status(const Process* process) {
        if ((process == NULL) || (process->parent_process_id == 0U)) {
            return;
        }

        for (Size index = 0U; index < exit_records.count(); ++index) {
            ProcessExitRecord* record = &exit_records[index];

            if ((record->process_id == process->id)
                && (record->parent_process_id == process->parent_process_id)) {
                record->exit_code = process->exit_code;
                return;
            }
        }

        (void)exit_records.append(ProcessExitRecord{ process->id, process->parent_process_id, process->exit_code });
    }

    /*
     * orphan_launched_processes
     *
     * Launcher metadata must not turn process lifetime into a dependency tree.
     * Once one process exits, any live peers it started keep running but lose
     * the stale launcher backlink so later waits and task inspection do not
     * point at a recycled PID.
     *
     * @param launcher_id PID of the process that is exiting.
     * @return Nothing.
     */
    void orphan_launched_processes(Pid launcher_id) {
        if (launcher_id == 0U) {
            return;
        }

        for (Process* process = process_head; process != NULL; process = process->next) {
            if (process->parent_process_id != launcher_id) {
                continue;
            }

            process->parent_process_id = 0U;
            process->parent = NULL;
        }
    }

    /*
     * Release every process-owned resource root before the slot disappears.
     *
     * The process object is the ownership boundary for user-mode resources in
     * this kernel. Teardown therefore needs one deterministic sweep that walks
     * each subsystem while the address space is still live, instead of relying
     * on later implicit cleanup after the process slot has already been torn
     * down.
     *
     * @param process Process whose owned resources should be destroyed.
     * @return Nothing.
     */
    void collect_process_resources(Process* process) {
        if ((process == NULL) || object_has_flag(process->header.flags, ObjectFlags::Kernel)) {
            return;
        }

        (void)GuiService::release_process_surfaces(process);
        (void)SharedMemoryManager::release_process_mappings(process);
        (void)UserHeap::release_process(process);
        (void)Loader::release_user_process_resources(process);
        (void)mm::MemoryManager::destroy_user_address_space(&process->process_address_space);
        process->handles = NULL;
    }

    Status create_process_with_mode(const char* name, bool user_process, Process** out_process) {
        Process* process;
        const AddressSpace* bootstrap_space;
        Status status;

        if (out_process == NULL) {
            return StatusInvalidArgument;
        }

        process = allocate_process_slot();
        if (process == NULL) {
            return StatusNoSpace;
        }

        memzero(process, sizeof(*process));
        process->header.type = ObjectType::Process;
        process->header.flags = user_process ? ObjectFlags::None : ObjectFlags::Kernel;
        process->id = next_pid++;
        process->header.object_id = process->id;
        process->header.ref_count = 1;
        process->current_state = ProcessState::Created;
        process->launch_arguments = NULL;
        process->loader_image_backing = NULL;
        process->loader_image_bytes = 0U;
        process->loader_stack_backing = NULL;
        process->loader_stack_bytes = 0U;
        process->parent_process_id = 0U;
        copy_text(process->name, COUNT_OF(process->name), name, user_process ? "user" : "process");

        if (user_process) {
            status = mm::MemoryManager::create_user_address_space(&process->process_address_space);
            if (status != StatusOK) {
                release_process_slot(process);
                return status;
            }
        }
        else {
            // Kernel processes inherit the bootstrap identity map so existing boot threads keep
            // the same low-memory view they were started with before EL0 support existed.
            bootstrap_space = mm::MemoryManager::bootstrap_address_space();
            if (bootstrap_space != NULL) {
                process->process_address_space = *bootstrap_space;
            }
        }

        process->process_address_space.asid = static_cast<U16>(process->id & 0xFFFFU);

        status = KernelObjectManager::register_object(&process->header);
        if (status != StatusOK) {
            if (user_process) {
                (void)mm::MemoryManager::destroy_user_address_space(&process->process_address_space);
            }
            release_process_slot(process);
            return status;
        }

        link_process(process);
        ++process_count_value;
        // Emit creation only after the process is registered so subscribers can resolve the identifiers immediately.
        KernelEventBroker::publish_process_event(
            KernelEventTypeProcessCreated,
            process->id,
            process->name,
            NULL,
            process->parent_process_id,
            static_cast<I64>(process->exit_code),
            static_cast<U32>(process->current_state),
            static_cast<U32>(process->header.flags),
            StatusOK);
        *out_process = process;
        return StatusOK;
    }

} // namespace

Status ProcessManager::init(void) {
    process_count_value = 0;
    next_pid = 1;
    process_head = NULL;
    process_tail = NULL;
    exit_records.clear();
    return StatusOK;
}

Status ProcessManager::create_process(const char* name, Process** out_process) {
    return create_process_with_mode(name, false, out_process);
}

Status ProcessManager::create_user_process(const char* name, Process** out_process) {
    return create_process_with_mode(name, true, out_process);
}

Status ProcessManager::destroy_process(Process* process) {
    Pid exiting_process_id;

    if (!contains_process(process)) {
        return StatusNotFound;
    }

    exiting_process_id = process->id;
    process->current_state = ProcessState::Terminated;
    while (process->thread_list_head != NULL) {
        Status status = ThreadManager::destroy_thread(process->thread_list_head);
        if (status != StatusOK) {
            return status;
        }
    }

    // Publish the terminal snapshot after thread teardown so observers see thread exits before the process disappears.
    KernelEventBroker::publish_process_event(
        KernelEventTypeProcessExited,
        process->id,
        process->name,
        NULL,
        process->parent_process_id,
        static_cast<I64>(process->exit_code),
        static_cast<U32>(process->current_state),
        static_cast<U32>(process->header.flags),
        StatusOK);
    // Keep the exit code available after the process slot is released so
    // waitPid can report the child result without a zombie process object.
    remember_exit_status(process);
    orphan_launched_processes(exiting_process_id);

    unlink_process(process);
    if (process_count_value != 0U) {
        --process_count_value;
    }

    collect_process_resources(process);
    if (process->launch_arguments != NULL) {
        Heap::free(process->launch_arguments);
        process->launch_arguments = NULL;
    }

    (void)KernelObjectManager::unregister_object(&process->header);
    // Drop any subscriptions owned by the exiting process before its slot can be recycled for another PID.
    KernelEventBroker::unsubscribe_process(process->id);
    release_process_slot(process);
    return StatusOK;
}

Status ProcessManager::terminate_process(Pid id, U64 exit_code) {
    Process* process = find_process(id);

    if (process == NULL) {
        return StatusNotFound;
    }

    process->current_state = ProcessState::Exiting;
    process->exit_code = exit_code;
    return destroy_process(process);
}

Status ProcessManager::reap_exiting(void) {
    Process* current_process = Scheduler::current_process();

    for (Process* process = process_head, *next_process = NULL; process != NULL; process = next_process) {
        next_process = process->next;
        if (process == current_process) {
            continue;
        }
        if (process->current_state != ProcessState::Exiting) {
            continue;
        }
        if (object_has_flag(process->header.flags, ObjectFlags::Permanent)) {
            continue;
        }

        (void)destroy_process(process);
    }

    return StatusOK;
}

Status ProcessManager::consume_exit_status(Pid id, Pid parent_id, U64* exit_code_out) {
    if (exit_code_out == NULL) {
        return StatusInvalidArgument;
    }

    for (Size index = 0U; index < exit_records.count(); ++index) {
        ProcessExitRecord* record = &exit_records[index];
        if ((record->process_id != id) || (record->parent_process_id != parent_id)) {
            continue;
        }

        *exit_code_out = record->exit_code;
        exit_records.remove_at(index);
        return StatusOK;
    }

    return StatusNotFound;
}

Process* ProcessManager::find_process(Pid id) {
    for (Process* process = process_head; process != NULL; process = process->next) {
        if (process->id == id) {
            return process;
        }
    }

    return NULL;
}

Process* ProcessManager::find_process(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (Process* process = process_head; process != NULL; process = process->next) {
        if (same_text(process->name, name)) {
            return process;
        }
    }

    return NULL;
}

Status ProcessManager::attach_thread(Process* process, Thread* thread) {
    if ((process == NULL) || (thread == NULL)) {
        return StatusInvalidArgument;
    }
    if (!contains_process(process)) {
        return StatusNotFound;
    }
    if (thread->parent == process) {
        return StatusAlreadyExists;
    }
    if (thread->parent != NULL) {
        return StatusBusy;
    }

    // Threads are linked twice: once globally by ThreadManager and once per-process here, so process teardown can walk only its own members.
    thread->parent = process;
    thread->process_prev = process->thread_list_tail;
    thread->process_next = NULL;

    if (process->thread_list_tail != NULL) {
        process->thread_list_tail->process_next = thread;
    }
    else {
        process->thread_list_head = thread;
    }

    process->thread_list_tail = thread;
    if (process->main_thread == NULL) {
        process->main_thread = thread;
    }
    ++process->thread_count;
    return StatusOK;
}

Status ProcessManager::detach_thread(Process* process, Thread* thread) {
    if ((process == NULL) || (thread == NULL)) {
        return StatusInvalidArgument;
    }
    if (thread->parent != process) {
        return StatusNotFound;
    }

    if (thread->process_prev != NULL) {
        thread->process_prev->process_next = thread->process_next;
    }
    else {
        process->thread_list_head = thread->process_next;
    }

    if (thread->process_next != NULL) {
        thread->process_next->process_prev = thread->process_prev;
    }
    else {
        process->thread_list_tail = thread->process_prev;
    }

    thread->parent = NULL;
    thread->process_next = NULL;
    thread->process_prev = NULL;

    if (process->main_thread == thread) {
        process->main_thread = process->thread_list_head;
    }
    if (process->thread_count != 0U) {
        --process->thread_count;
    }

    return StatusOK;
}

Size ProcessManager::process_count(void) {
    return process_count_value;
}