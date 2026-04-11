#include "service_call.h"

#include "arch/aarch64/exception_debugger.h"
#include "debug-message.h"
#include "dll_loader.h"
#include "gui_service.h"
#include "heap.h"
#include "kernel_event.h"
#include "kernel_event_broker.h"
#include "loader.h"
#include "kernel_time.h"
#include "mm.h"
#include "platform.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "shared_memory.h"
#include "thread.h"
#include "user_heap.h"
#include "user_ipc.h"
#include "vfs.h"

extern "C" [[noreturn]] void scheduler_thread_exit_current(void);

namespace {

    inline constexpr U64 UserLoaderActionBit = 1ULL << 63;
    inline constexpr Size ServicePathCapacity = 260U;
    inline constexpr Size ServiceTextCapacity = 512U;
    inline constexpr Size VfsReadProgressMinimumBytes = 4UL * 1024UL;
    inline constexpr U64 SyscallCount = 42ULL;
    inline constexpr U32 IpcQueueInitialCapacity = 32U;

    typedef struct UserDirectoryEntry {
        char name[128];
        unsigned long size;
        unsigned long attr;
    } UserDirectoryEntry;

    typedef struct UserTaskInfo {
        long id;
        long thread_id;
        long parent_process_id;
        long main_thread_state;
        long wait_reason;
        long exit_code;
        long scheduler_ticks;
        long current_priority;
        unsigned long flags;
        char name[32];
    } UserTaskInfo;

    typedef struct UserMemInfo {
        unsigned long total_bytes;
        unsigned long free_bytes;
        unsigned long page_size;
        unsigned long free_pages;
    } UserMemInfo;

    typedef struct IpcProcessQueue {
        U64 pid;
        UserIpcMessage* messages;
        U32 capacity;
        U32 head;
        U32 tail;
        U32 count;
        IpcProcessQueue* next;
        IpcProcessQueue* prev;
    } IpcProcessQueue;

    typedef Status(*ServiceHandler)(ServiceFrame* frame);

    typedef struct DirectoryLookupContext {
        U64 target_index;
        U64 current_index;
        bool found;
        UserDirectoryEntry* entry;
    } DirectoryLookupContext;

    IpcProcessQueue* g_ipc_queue_head;
    IpcProcessQueue* g_ipc_queue_tail;

    /**
     * Report whether one character is ASCII whitespace.
     *
     * The debug-shell parser stays local and libc-free so the service path keeps
     * the same bootstrap properties as the rest of the kernel.
     *
     * @param ch Character to classify.
     * @return True when the character separates tokens.
     */
    bool is_space(char ch) {
        return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
    }

    /**
     * Fold one ASCII letter to lowercase.
     *
     * @param ch Character to normalize.
     * @return Lowercase ASCII letter, or the original byte when no fold exists.
     */
    char ascii_lower(char ch) {
        if ((ch >= 'A') && (ch <= 'Z')) {
            return static_cast<char>(ch - 'A' + 'a');
        }

        return ch;
    }

    /**
     * Compare two null-terminated strings case-insensitively in ASCII.
     *
     * @param lhs Left-hand string.
     * @param rhs Right-hand string.
     * @return True when both strings contain the same ASCII text.
     */
    bool same_text_case_insensitive(const char* lhs, const char* rhs) {
        Size index = 0U;

        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((lhs[index] != '\0') && (rhs[index] != '\0')) {
            if (ascii_lower(lhs[index]) != ascii_lower(rhs[index])) {
                return false;
            }
            ++index;
        }

        return lhs[index] == rhs[index];
    }

    /**
     * Extract one whitespace-delimited token from a shell command line.
     *
     * @param cursor In-out cursor that advances past the copied token.
     * @param buffer Destination token buffer.
     * @param capacity Destination buffer capacity.
     * @return True when a token was copied successfully.
     */
    bool next_token(const char** cursor, char* buffer, Size capacity) {
        Size length = 0U;
        const char* input;

        if ((cursor == NULL) || (buffer == NULL) || (capacity == 0U)) {
            return false;
        }

        input = *cursor;
        while ((input != NULL) && is_space(*input)) {
            ++input;
        }
        if ((input == NULL) || (*input == '\0')) {
            return false;
        }

        while ((input[length] != '\0') && !is_space(input[length])) {
            if ((length + 1U) >= capacity) {
                return false;
            }
            buffer[length] = input[length];
            ++length;
        }

        buffer[length] = '\0';
        *cursor = &input[length];
        return true;
    }

    /**
     * Print the supported kernel debug-shell commands.
     *
     * The user-space shell already owns command discovery; this lightweight help
     * text only documents the few debugger verbs implemented in the live kernel.
     */
    void write_debug_shell_help(void) {
        board::Serial::puts(
            "[kernel] kdebug commands:\n"
            "  help      Show this help\n"
            "  break     Enter the exception debugger on the current trap frame\n"
            "  enter     Alias for 'break'\n"
            "  debugger  Alias for 'break'\n");
    }

    U64 encode_status(Status status) {
        return static_cast<U64>(static_cast<I64>(status));
    }

    bool user_range_valid(const void* address, Size size) {
        VirtAddr start;
        VirtAddr end;

        if ((address == NULL) || (reinterpret_cast<VirtAddr>(address) == 0U)) {
            return false;
        }

        start = reinterpret_cast<VirtAddr>(address);
        if (!mm::MemoryManager::is_user_address(start)) {
            return false;
        }
        if (size == 0U) {
            return true;
        }

        end = start + size - 1U;
        if (end < start) {
            return false;
        }

        return mm::MemoryManager::is_user_address(end);
    }

    Status copy_user_string(const char* user_text, char* kernel_text, Size capacity) {
        Size index = 0U;

        if ((user_text == NULL) || (kernel_text == NULL) || (capacity == 0U)) {
            return StatusInvalidArgument;
        }
        if (!user_range_valid(user_text, 1U)) {
            return StatusInvalidArgument;
        }

        while ((index + 1U) < capacity) {
            char ch = user_text[index];

            kernel_text[index] = ch;
            if (ch == '\0') {
                return StatusOK;
            }
            ++index;
        }

        kernel_text[capacity - 1U] = '\0';
        return StatusNoSpace;
    }

    Status copy_user_bytes_to_kernel(const void* user_buffer, void* kernel_buffer, Size size) {
        if ((user_buffer == NULL) || ((kernel_buffer == NULL) && (size != 0U))) {
            return StatusInvalidArgument;
        }
        if ((size != 0U) && !user_range_valid(user_buffer, size)) {
            return StatusInvalidArgument;
        }

        if (size != 0U) {
            memcopy(kernel_buffer, user_buffer, size);
        }
        return StatusOK;
    }

    /**
     * Report whether one process id still resolves to a live process object.
     *
     * @param pid Target process identifier.
     * @return True when the process still exists and is not fully terminated.
     */
    bool ipc_process_alive(U64 pid) {
        Process* process = ProcessManager::find_process(pid);

        return (process != NULL) && (process->current_state != ProcessState::Terminated);
    }

    /**
     * Drop one queue slot back to its empty state.
     *
     * @param queue Queue slot to clear.
     * @return Nothing.
     */
    void ipc_clear_queue(IpcProcessQueue* queue) {
        if (queue == NULL) {
            return;
        }

        if (queue->messages != NULL) {
            Heap::free(queue->messages);
        }
        memzero(queue, sizeof(*queue));
    }

    /**
     * Link one IPC queue record into the global broker list.
     *
     * @param queue Queue record to publish.
     * @return Nothing.
     */
    void ipc_link_queue(IpcProcessQueue* queue) {
        if (queue == NULL) {
            return;
        }

        queue->prev = g_ipc_queue_tail;
        queue->next = NULL;
        if (g_ipc_queue_tail != NULL) {
            g_ipc_queue_tail->next = queue;
        }
        else {
            g_ipc_queue_head = queue;
        }

        g_ipc_queue_tail = queue;
    }

    /**
     * Unlink one IPC queue record from the global broker list.
     *
     * @param queue Queue record to remove.
     * @return Nothing.
     */
    void ipc_unlink_queue(IpcProcessQueue* queue) {
        if (queue == NULL) {
            return;
        }

        if (queue->prev != NULL) {
            queue->prev->next = queue->next;
        }
        else {
            g_ipc_queue_head = queue->next;
        }

        if (queue->next != NULL) {
            queue->next->prev = queue->prev;
        }
        else {
            g_ipc_queue_tail = queue->prev;
        }

        queue->next = NULL;
        queue->prev = NULL;
    }

    /**
     * Release one IPC queue record and its buffered messages.
     *
     * @param queue Queue record to destroy.
     * @return Nothing.
     */
    void ipc_destroy_queue(IpcProcessQueue* queue) {
        if (queue == NULL) {
            return;
        }

        ipc_unlink_queue(queue);
        ipc_clear_queue(queue);
        Heap::free(queue);
    }

    /**
     * Grow one message queue so a new packet can be appended.
     *
     * @param queue Queue being grown.
     * @param required_capacity Minimum slot count required after the call.
     * @return StatusOK on success, or StatusNoMemory when growth fails.
     */
    Status ipc_reserve_message_capacity(IpcProcessQueue* queue, U32 required_capacity) {
        UserIpcMessage* messages;
        U32 grown_capacity;

        if (queue == NULL) {
            return StatusInvalidArgument;
        }
        if (required_capacity <= queue->capacity) {
            return StatusOK;
        }

        grown_capacity = (queue->capacity == 0U) ? IpcQueueInitialCapacity : queue->capacity;
        while (grown_capacity < required_capacity) {
            if (grown_capacity > (0xFFFFFFFFU / 2U)) {
                grown_capacity = required_capacity;
                break;
            }

            grown_capacity *= 2U;
        }

        messages = static_cast<UserIpcMessage*>(Heap::alloc(sizeof(UserIpcMessage) * grown_capacity, alignof(UserIpcMessage)));
        if (messages == NULL) {
            return StatusNoMemory;
        }

        for (U32 index = 0U; index < queue->count; ++index) {
            messages[index] = queue->messages[(queue->head + index) % queue->capacity];
        }

        if (queue->messages != NULL) {
            Heap::free(queue->messages);
        }

        queue->messages = messages;
        queue->capacity = grown_capacity;
        queue->head = 0U;
        queue->tail = queue->count;
        return StatusOK;
    }

    /**
     * Sweep dead-process queues so stale recipients do not hold broker memory.
     *
     * @return Nothing.
     */
    void ipc_sweep_dead_queues(void) {
        for (IpcProcessQueue* queue = g_ipc_queue_head, *next_queue = NULL; queue != NULL; queue = next_queue) {
            next_queue = queue->next;
            if (!ipc_process_alive(queue->pid)) {
                ipc_destroy_queue(queue);
            }
        }
    }

    /**
     * Find one existing queue for a process.
     *
     * @param pid Target process identifier.
     * @return Matching queue, or NULL when the process has no broker state yet.
     */
    IpcProcessQueue* ipc_find_queue(U64 pid) {
        for (IpcProcessQueue* queue = g_ipc_queue_head; queue != NULL; queue = queue->next) {
            if (queue->pid == pid) {
                return queue;
            }
        }

        return NULL;
    }

    /**
     * Return an existing queue or allocate a new empty queue for a process.
     *
     * @param pid Target process identifier.
     * @return Queue pointer on success, or NULL when the broker is full.
     */
    IpcProcessQueue* ipc_reserve_queue(U64 pid) {
        IpcProcessQueue* queue = ipc_find_queue(pid);

        if (queue != NULL) {
            return queue;
        }

        queue = static_cast<IpcProcessQueue*>(Heap::alloc(sizeof(IpcProcessQueue), alignof(IpcProcessQueue)));
        if (queue == NULL) {
            return NULL;
        }

        memzero(queue, sizeof(*queue));
        queue->pid = pid;
        ipc_link_queue(queue);
        return queue;
    }

    /**
     * Wake one thread in the destination process that is blocked waiting for a
     * message so the new queue entry becomes observable immediately.
     *
     * @param pid Destination process identifier.
     * @return Nothing.
     */
    void ipc_wake_waiter(U64 pid) {
        Process* process = ProcessManager::find_process(pid);

        if (process == NULL) {
            return;
        }

        for (Thread* thread = process->thread_list_head; thread != NULL; thread = thread->process_next) {
            if ((thread->current_state == ThreadState::Waiting)
                && (thread->wait_status == static_cast<U32>(WaitReason::Message))) {
                (void)Scheduler::enqueue(thread);
                return;
            }
        }
    }

    /**
     * Append one message to the destination queue.
     *
     * @param receiver_pid Destination process identifier.
     * @param message Packet to enqueue.
      * @return StatusOK on success, or a negative broker status when growth or
      * queue creation fails.
     */
    Status ipc_enqueue(U64 receiver_pid, const UserIpcMessage* message) {
        IpcProcessQueue* queue;
        Status status;

        if ((message == NULL) || !ipc_process_alive(receiver_pid)) {
            return StatusNotFound;
        }

        queue = ipc_reserve_queue(receiver_pid);
        if (queue == NULL) {
            return StatusNoMemory;
        }

        status = ipc_reserve_message_capacity(queue, queue->count + 1U);
        if (status != StatusOK) {
            return status;
        }

        queue->messages[queue->tail] = *message;
        queue->tail = (queue->tail + 1U) % queue->capacity;
        ++queue->count;
        ipc_wake_waiter(receiver_pid);
        return StatusOK;
    }


    extern "C" Status service_kernel_ipc_notify(
        U64 receiver_pid,
        unsigned long protocol,
        unsigned long kind,
        unsigned long arg0,
        unsigned long arg1,
        unsigned long arg2,
        unsigned long arg3) {
        UserIpcMessage message;

        memzero(&message, sizeof(message));
        message.sender_pid = 0L;
        message.receiver_pid = static_cast<long>(receiver_pid);
        message.protocol = protocol;
        message.kind = kind;
        message.arg0 = arg0;
        message.arg1 = arg1;
        message.arg2 = arg2;
        message.arg3 = arg3;
        return ipc_enqueue(receiver_pid, &message);
    }
    /**
     * Pop the oldest queued message for one process.
     *
     * @param receiver_pid Receiving process identifier.
     * @param out_message Receives the dequeued packet.
     * @return StatusOK on success, StatusBusy when the queue is empty, or
     * StatusNotFound when the process is gone.
     */
    Status ipc_dequeue(U64 receiver_pid, UserIpcMessage* out_message) {
        IpcProcessQueue* queue;

        if ((out_message == NULL) || !ipc_process_alive(receiver_pid)) {
            return StatusNotFound;
        }

        queue = ipc_find_queue(receiver_pid);
        if ((queue == NULL) || (queue->count == 0U)) {
            return StatusBusy;
        }

        *out_message = queue->messages[queue->head];
        queue->head = (queue->head + 1U) % queue->capacity;
        --queue->count;
        if (queue->count == 0U) {
            queue->head = 0U;
            queue->tail = 0U;
        }
        return StatusOK;
    }

    /**
     * Allocate and copy one optional user string into heap-backed kernel memory.
     *
     * @param user_text Optional user-mode string pointer.
     * @param kernel_text_out Receives the allocated kernel copy.
     * @return StatusOK on success, or a propagated allocation/copy failure.
     */
    Status copy_optional_user_string_alloc(const char* user_text, char** kernel_text_out) {
        Size length = 0U;
        char* text;

        if (kernel_text_out == NULL) {
            return StatusInvalidArgument;
        }
        if (user_text == NULL) {
            *kernel_text_out = NULL;
            return StatusOK;
        }
        if (!user_range_valid(user_text, 1U)) {
            return StatusInvalidArgument;
        }

        while (user_text[length] != '\0') {
            ++length;
        }

        text = static_cast<char*>(Heap::alloc(length + 1U, alignof(char)));
        if (text == NULL) {
            return StatusNoMemory;
        }

        for (Size index = 0U; index < length; ++index) {
            text[index] = user_text[index];
        }
        text[length] = '\0';
        *kernel_text_out = text;
        return StatusOK;
    }

    Status copy_optional_user_string(const char* user_text, char* kernel_text, Size capacity) {
        if ((kernel_text == NULL) || (capacity == 0U)) {
            return StatusInvalidArgument;
        }
        if (user_text == NULL) {
            kernel_text[0] = '\0';
            return StatusOK;
        }

        return copy_user_string(user_text, kernel_text, capacity);
    }

    Status copy_kernel_string_to_user(const char* kernel_text, char* user_text, Size capacity) {
        Size index = 0U;

        if ((kernel_text == NULL) || (user_text == NULL) || (capacity == 0U)) {
            return StatusInvalidArgument;
        }
        if (!user_range_valid(user_text, capacity)) {
            return StatusInvalidArgument;
        }

        while ((index + 1U) < capacity && (kernel_text[index] != '\0')) {
            user_text[index] = kernel_text[index];
            ++index;
        }

        user_text[index] = '\0';
        return StatusOK;
    }

    Status copy_kernel_bytes_to_user(void* user_buffer, const void* kernel_buffer, Size size) {
        if ((user_buffer == NULL) || ((kernel_buffer == NULL) && (size != 0U))) {
            return StatusInvalidArgument;
        }
        if (!user_range_valid(user_buffer, size == 0U ? 1U : size)) {
            return StatusInvalidArgument;
        }

        if (size != 0U) {
            memcopy(user_buffer, kernel_buffer, size);
        }
        return StatusOK;
    }

    /**
     * Calculate the readable byte span for one present-buffer upload.
     *
     * The rectangle may expose a larger stride than its visible width, so the
     * validation size must cover the last row's final visible pixel rather than
     * only `width * height * 4` bytes.
     *
     * @param buffer Present request copied from user mode.
     * @return Required readable byte span, or zero when the request is invalid.
     */
    Size gui_present_user_span(const RosKernelGuiPresentBuffer* buffer) {
        U64 span;

        if ((buffer == NULL) || (buffer->width == 0U) || (buffer->height == 0U) || (buffer->pitch < (buffer->width * sizeof(U32)))) {
            return 0U;
        }

        span = (static_cast<U64>(buffer->height - 1U) * static_cast<U64>(buffer->pitch)) + (static_cast<U64>(buffer->width) * sizeof(U32));
        if (span == 0ULL || span > static_cast<U64>(~static_cast<Size>(0U))) {
            return 0U;
        }

        return static_cast<Size>(span);
    }

    bool is_drive_letter(char ch) {
        if ((ch >= 'a') && (ch <= 'z')) {
            ch = static_cast<char>(ch - ('a' - 'A'));
        }

        return (ch >= 'A') && (ch <= 'Z');
    }

    Status translate_user_path(const char* user_path, char* kernel_path, Size capacity) {
        Size write_index = 0U;
        const char* cursor = user_path;

        if ((user_path == NULL) || (kernel_path == NULL) || (capacity < 4U)) {
            return StatusInvalidArgument;
        }
        if (user_path[0] == '\0') {
            return StatusInvalidArgument;
        }

        if (is_drive_letter(user_path[0]) && (user_path[1] == ':')) {
            while ((cursor[write_index] != '\0') && ((write_index + 1U) < capacity)) {
                kernel_path[write_index] = cursor[write_index];
                ++write_index;
            }
            if (cursor[write_index] != '\0') {
                return StatusNoSpace;
            }
            kernel_path[write_index] = '\0';
            return StatusOK;
        }

        if ((user_path[0] == '/') || (user_path[0] == '\\')) {
            kernel_path[write_index++] = 'C';
            kernel_path[write_index++] = ':';
        }
        else {
            kernel_path[write_index++] = 'C';
            kernel_path[write_index++] = ':';
            kernel_path[write_index++] = '/';
        }

        while (*cursor != '\0') {
            if ((write_index + 1U) >= capacity) {
                return StatusNoSpace;
            }
            kernel_path[write_index++] = ((*cursor == '\\') ? '/' : *cursor);
            ++cursor;
        }

        kernel_path[write_index] = '\0';
        return StatusOK;
    }

    Status copy_user_path(const char* user_path, char* kernel_path, Size capacity) {
        char user_copy[ServicePathCapacity];
        Status status;

        if (capacity > sizeof(user_copy)) {
            return StatusInvalidArgument;
        }

        status = copy_user_string(user_path, user_copy, sizeof(user_copy));
        if (status != StatusOK) {
            return status;
        }

        return translate_user_path(user_copy, kernel_path, capacity);
    }

    void fill_directory_entry(UserDirectoryEntry* entry, const char* name, const VfsNode* node) {
        Size index = 0U;

        memzero(entry, sizeof(*entry));
        while ((name[index] != '\0') && ((index + 1U) < COUNT_OF(entry->name))) {
            entry->name[index] = name[index];
            ++index;
        }
        entry->name[index] = '\0';
        entry->size = static_cast<unsigned long>(node->size_bytes);
        entry->attr = (node->type == VfsNodeTypeDirectory) ? 0x10UL : 0UL;
    }

    Status directory_lookup_visitor(const char* name, const VfsNode* node, void* context) {
        DirectoryLookupContext* lookup = static_cast<DirectoryLookupContext*>(context);

        if ((lookup == NULL) || (lookup->entry == NULL) || (name == NULL) || (node == NULL)) {
            return StatusInvalidArgument;
        }

        if (lookup->current_index == lookup->target_index) {
            fill_directory_entry(lookup->entry, name, node);
            lookup->found = true;
            return StatusBusy;
        }

        ++lookup->current_index;
        return StatusOK;
    }

    /**
     * Populate one userspace task snapshot from a kernel process object.
     *
     * Task inspection commands care about what the scheduler is doing with the
     * process main thread right now, not just whether the process object still
     * exists. Exporting the main-thread state and wait reason lets `ps`
     * distinguish READY, RUN, SLEEP, and other blocked states accurately.
     *
     * @param process Kernel process being exported to userspace.
     * @param info Destination snapshot that will be copied back through the syscall.
     * @return Nothing.
     */
    void fill_task_info(Process* process, UserTaskInfo* info) {
        Size index = 0U;
        const Thread* main_thread = process->main_thread;

        memzero(info, sizeof(*info));
        info->id = static_cast<long>(process->id);
        info->thread_id = static_cast<long>((main_thread != NULL) ? main_thread->id : 0U);
        info->parent_process_id = static_cast<long>(process->parent_process_id);
        // The shell's `ps` command needs the runnable vs waiting state of the
        // task's main thread. Process state is too coarse because a sleeping
        // thread still leaves the process marked Running.
        info->main_thread_state = static_cast<long>((main_thread != NULL) ? main_thread->current_state : ThreadState::Terminated);
        // Wait reason is only meaningful while a thread is parked, but exporting
        // it all the time keeps the ABI simple and lets userspace show a stable
        // placeholder when the task is runnable.
        info->wait_reason = static_cast<long>((main_thread != NULL) ? main_thread->wait_status : static_cast<U32>(WaitReason::None));
        info->exit_code = static_cast<long>(process->exit_code);
        info->scheduler_ticks = static_cast<long>(Scheduler::tick_count());
        info->current_priority = static_cast<long>((main_thread != NULL) ? main_thread->current_priority : ThreadPriorityIdle);
        info->flags = static_cast<unsigned long>(process->header.flags);

        while ((process->name[index] != '\0') && ((index + 1U) < COUNT_OF(info->name))) {
            info->name[index] = process->name[index];
            ++index;
        }
        info->name[index] = '\0';
    }

    Status service_unsupported(ServiceFrame* frame) {
        frame->result = encode_status(StatusNotSupported);
        return StatusNotSupported;
    }

    Status service_write(ServiceFrame* frame) {
        char buffer[ServiceTextCapacity];
        Status status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), buffer, sizeof(buffer));

        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        board::Serial::puts(buffer);
        frame->result = 0U;
        return StatusOK;
    }

    Status service_malloc(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        VirtAddr address = 0U;
        Status status;

        if ((frame == NULL) || (process == NULL)) {
            if (frame != NULL) {
                frame->result = encode_status(StatusInvalidArgument);
            }
            return StatusInvalidArgument;
        }

        status = UserHeap::alloc_raw(process, static_cast<Size>(frame->arguments[0]), &address);
        frame->result = (status == StatusOK) ? address : encode_status(status);
        return status;
    }

    Status service_free(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        Status status;

        if ((frame == NULL) || (process == NULL)) {
            if (frame != NULL) {
                frame->result = encode_status(StatusInvalidArgument);
            }
            return StatusInvalidArgument;
        }
        if (frame->arguments[0] == 0U) {
            frame->result = 0U;
            return StatusOK;
        }

        status = UserHeap::free_raw(process, static_cast<VirtAddr>(frame->arguments[0]));
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_exit(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();

        if (process != NULL) {
            process->current_state = ProcessState::Exiting;
            process->exit_code = frame->arguments[0];
        }

        scheduler_thread_exit_current();
    }

    Status service_get_param(ServiceFrame* frame) {
        (void)frame;
        frame->result = 0U;
        return StatusOK;
    }

    Status service_sleep(ServiceFrame* frame) {
        const U64 sleep_msec = frame->arguments[0];
        const U64 target_ticks = KernelTime::milliseconds_to_ticks_ceil(sleep_msec);
        Status status;

        if (target_ticks == 0U) {
            frame->result = 0U;
            return StatusOK;
        }

        status = Scheduler::sleep_current(target_ticks);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        frame->result = 0U;
        return StatusOK;
    }

    Status service_task_name(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        char* user_buffer = reinterpret_cast<char*>(frame->arguments[0]);
        Size buffer_size = static_cast<Size>(frame->arguments[1]);
        Status status;

        if ((process == NULL) || (buffer_size == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_kernel_string_to_user(process->name, user_buffer, buffer_size);
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    /**
     * Map one named shared-memory object into the current process.
     *
     * This service intentionally reuses the dormant `SYS_SHLIB_LOCAL` ABI slot
     * because it already matches the needed `(name, size)` argument shape and
     * had no live implementation in the current kernel.
     *
     * @param frame Active syscall frame.
     * @return StatusOK on success, or a negative kernel status code.
     */
    Status service_shared_memory_acquire(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        char object_name[ServicePathCapacity];
        VirtAddr view_address = 0U;
        Status status;

        if (process == NULL) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), object_name, sizeof(object_name));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        status = SharedMemoryManager::acquire(process, object_name, static_cast<Size>(frame->arguments[1]), &view_address);
        frame->result = (status == StatusOK) ? view_address : encode_status(status);
        return status;
    }

    Status service_spawn(ServiceFrame* frame) {
        char kernel_path[ServicePathCapacity];
        char process_name[ProcessNameCapacity];
        char* launch_arguments = NULL;
        Thread* main_thread = NULL;
        Process* parent_process = Scheduler::current_process();
        Process* child_process;
        Status status;

        (void)Heap::debug_validate("service_spawn:entry");

        status = copy_user_path(reinterpret_cast<const char*>(frame->arguments[0]), kernel_path, sizeof(kernel_path));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        status = copy_optional_user_string(reinterpret_cast<const char*>(frame->arguments[1]), process_name, sizeof(process_name));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        status = copy_optional_user_string_alloc(reinterpret_cast<const char*>(frame->arguments[2]), &launch_arguments);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        // Reuse the same loader path for boot-time and shell-driven launches so
        // the kernel maintains one process-creation implementation.
        status = Loader::spawn_user_process(
            kernel_path,
            process_name[0] != '\0' ? process_name : NULL,
            launch_arguments != NULL ? launch_arguments : "",
            &main_thread);
        if (launch_arguments != NULL) {
            Heap::free(launch_arguments);
            launch_arguments = NULL;
        }
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }
        if ((main_thread == NULL) || (main_thread->parent == NULL)) {
            frame->result = encode_status(StatusFault);
            return StatusFault;
        }

        child_process = main_thread->parent;
        if (parent_process != NULL) {
            child_process->parent = parent_process;
            child_process->parent_process_id = parent_process->id;
        }

        status = Scheduler::enqueue(main_thread);
        if (status != StatusOK) {
            (void)ProcessManager::destroy_process(child_process);
            frame->result = encode_status(status);
            return status;
        }

        frame->result = child_process->id;
        return StatusOK;
    }

    Status service_wait_pid(ServiceFrame* frame) {
        Process* parent_process = Scheduler::current_process();
        Pid child_pid = static_cast<Pid>(frame->arguments[0]);
        long* user_result = reinterpret_cast<long*>(frame->arguments[1]);
        Status status;

        if ((parent_process == NULL) || (child_pid == 0U) || !user_range_valid(user_result, sizeof(*user_result))) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        for (;;) {
            U64 exit_code = 0U;
            Process* child_process;

            (void)ProcessManager::reap_exiting();
            status = ProcessManager::consume_exit_status(child_pid, parent_process->id, &exit_code);
            if (status == StatusOK) {
                long exit_code_copy = static_cast<long>(static_cast<I64>(exit_code));

                status = copy_kernel_bytes_to_user(user_result, &exit_code_copy, sizeof(exit_code_copy));
                frame->result = (status == StatusOK) ? 0U : encode_status(status);
                return status;
            }

            child_process = ProcessManager::find_process(child_pid);
            if ((child_process == NULL) || (child_process->parent_process_id != parent_process->id)) {
                frame->result = encode_status(StatusNotFound);
                return StatusNotFound;
            }

            // Waiting must both advance cooperative timer state for sleeping
            // children and yield the CPU so runnable children can continue.
            Scheduler::poll();
            Scheduler::yield();
        }
    }

    Status service_console_read(ServiceFrame* frame) {
        frame->result = static_cast<U64>(static_cast<unsigned char>(board::Serial::getc()));
        return StatusOK;
    }

    Status service_read_file(ServiceFrame* frame) {
        char kernel_path[ServicePathCapacity];
        VfsNode node;
        void* user_buffer = reinterpret_cast<void*>(frame->arguments[2]);
        Size length = static_cast<Size>(frame->arguments[3]);
        Size readable_bytes = 0U;
        Status status;
        SSize read_result;

        if ((length != 0U) && !user_range_valid(user_buffer, length)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_user_path(reinterpret_cast<const char*>(frame->arguments[0]), kernel_path, sizeof(kernel_path));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        memzero(&node, sizeof(node));
        status = VirtualFileSystem::resolve(kernel_path, &node);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }
        if (node.type != VfsNodeTypeFile) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        readable_bytes = length;
        if (frame->arguments[1] >= node.size_bytes) {
            readable_bytes = 0U;
        }
        else if (node.size_bytes - frame->arguments[1] < readable_bytes) {
            readable_bytes = static_cast<Size>(node.size_bytes - frame->arguments[1]);
        }

        if ((kernel_debug_zone_mask() == 0U) || (readable_bytes == 0U) || (readable_bytes < VfsReadProgressMinimumBytes)) {
            read_result = VirtualFileSystem::read(&node, frame->arguments[1], user_buffer, length);
            if (read_result < 0) {
                frame->result = encode_status(static_cast<Status>(read_result));
                return static_cast<Status>(read_result);
            }
        }
        else {
            U8* destination = static_cast<U8*>(user_buffer);
            U64 total_read = 0U;
            unsigned long next_progress = 20UL;
            const Size progress_chunk = (readable_bytes + 4U) / 5U;

            while (total_read < readable_bytes) {
                const Size remaining = readable_bytes - static_cast<Size>(total_read);
                const Size chunk_bytes = (remaining < progress_chunk) ? remaining : progress_chunk;

                read_result = VirtualFileSystem::read(
                    &node,
                    frame->arguments[1] + total_read,
                    destination + total_read,
                    chunk_bytes);
                if (read_result < 0) {
                    frame->result = encode_status(static_cast<Status>(read_result));
                    return static_cast<Status>(read_result);
                }
                if (read_result == 0) {
                    break;
                }

                total_read += static_cast<U64>(read_result);
                while ((next_progress <= 100UL) && ((total_read * 100ULL) >= (static_cast<U64>(readable_bytes) * next_progress))) {
                    KRETAIL(
                        "[vfs] read %s %lu%% (%llu/%llu)\n",
                        kernel_path,
                        next_progress,
                        static_cast<unsigned long long>(total_read),
                        static_cast<unsigned long long>(readable_bytes));
                    next_progress += 20UL;
                }
            }

            read_result = static_cast<SSize>(total_read);
        }

        (void)Heap::debug_validate("service_read_file:success");
        frame->result = static_cast<U64>(read_result);
        return StatusOK;
    }

    Status service_directory_entry(ServiceFrame* frame) {
        char kernel_path[ServicePathCapacity];
        VfsNode node;
        DirectoryLookupContext lookup = {};
        UserDirectoryEntry entry;
        Status status;

        status = copy_user_path(reinterpret_cast<const char*>(frame->arguments[0]), kernel_path, sizeof(kernel_path));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        memzero(&node, sizeof(node));
        status = VirtualFileSystem::resolve(kernel_path, &node);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }
        if (node.type != VfsNodeTypeDirectory) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        lookup.target_index = frame->arguments[1];
        lookup.current_index = 0U;
        lookup.found = false;
        lookup.entry = &entry;
        status = VirtualFileSystem::enumerate(&node, &lookup, &directory_lookup_visitor);
        if ((status != StatusOK) && (status != StatusBusy)) {
            frame->result = encode_status(status);
            return status;
        }
        if (!lookup.found) {
            frame->result = 0U;
            return StatusOK;
        }

        status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[2]), &entry, sizeof(entry));
        frame->result = (status == StatusOK) ? 1U : encode_status(status);
        return status;
    }

    Status service_mkdir(ServiceFrame* frame) {
        char kernel_path[ServicePathCapacity];
        Status status = copy_user_path(reinterpret_cast<const char*>(frame->arguments[0]), kernel_path, sizeof(kernel_path));

        if (status == StatusOK) {
            status = VirtualFileSystem::create(kernel_path, VfsNodeTypeDirectory, NULL);
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    /**
     * Remove one file or directory path from the userspace namespace.
     *
     * The refreshed userspace shell is moving toward a Windows CE style command
     * surface where DEL and RD are first-class verbs. Exposing VFS removal
     * through the syscall layer keeps that shell behavior native instead of
     * routing file deletion through legacy helpers or test-only code paths.
     *
     * @param frame Active syscall frame.
     * @return StatusOK on success, or the propagated VFS failure code.
     */
    Status service_remove(ServiceFrame* frame) {
        char kernel_path[ServicePathCapacity];
        Status status = copy_user_path(reinterpret_cast<const char*>(frame->arguments[0]), kernel_path, sizeof(kernel_path));

        if (status == StatusOK) {
            status = VirtualFileSystem::remove(kernel_path);
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_task_info(ServiceFrame* frame) {
        Process* process = ProcessManager::find_process(static_cast<Pid>(frame->arguments[0]));
        UserTaskInfo info;
        Status status;

        if (process == NULL) {
            frame->result = encode_status(StatusNotFound);
            return StatusNotFound;
        }

        fill_task_info(process, &info);
        status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[1]), &info, sizeof(info));
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_mem_info(ServiceFrame* frame) {
        HeapStats heap_stats;
        UserMemInfo info;
        Status status;

        Heap::get_stats(&heap_stats);
        info.total_bytes = static_cast<unsigned long>(heap_stats.total_bytes);
        info.free_bytes = static_cast<unsigned long>(heap_stats.free_bytes);
        info.page_size = static_cast<unsigned long>(mm::PageSize);
        info.free_pages = (info.page_size == 0UL) ? 0UL : (info.free_bytes / info.page_size);
        status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[0]), &info, sizeof(info));
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_reboot(ServiceFrame* frame) {
        (void)frame;
        board::Platform::power_off();
    }

    /**
     * Execute one narrow kernel debug-shell command.
     *
     * The legacy tree exposed a broad in-kernel shell, but the live kernel only
     * needs a deliberate entry point into the exception debugger. Keeping the
     * parser small avoids dragging a second command framework back into the
     * service layer.
     *
     * @param frame Active syscall frame.
     * @return StatusOK when the command ran, or an error for invalid input.
     */
    Status service_debug_shell(ServiceFrame* frame) {
        char command_line[ServiceTextCapacity];
        char command[32];
        const char* cursor = command_line;
        Status status;

        if (frame == NULL) {
            return StatusInvalidArgument;
        }

        status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), command_line, sizeof(command_line));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }
        if (!next_token(&cursor, command, sizeof(command))) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        if (same_text_case_insensitive(command, "help")) {
            write_debug_shell_help();
            frame->result = 0U;
            return StatusOK;
        }

        if (same_text_case_insensitive(command, "break")
            || same_text_case_insensitive(command, "enter")
            || same_text_case_insensitive(command, "debugger")) {
            AArch64ExceptionFrame* trap_frame = static_cast<AArch64ExceptionFrame*>(frame->trap_frame);

            if (trap_frame == NULL) {
                frame->result = encode_status(StatusFault);
                return StatusFault;
            }

            aarch64_exception_debugger_enter_deliberate(trap_frame);
            frame->result = 0U;
            return StatusOK;
        }

        board::Serial::puts("[kernel] kdebug: unknown command\n");
        write_debug_shell_help();
        frame->result = encode_status(StatusNotSupported);
        return StatusNotSupported;
    }

    Status service_task_args(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        const char* launch_arguments = "";
        Status status;

        if ((process != NULL) && (process->launch_arguments != NULL) && (process->launch_arguments[0] != '\0')) {
            launch_arguments = process->launch_arguments;
        }

        status = copy_kernel_string_to_user(launch_arguments, reinterpret_cast<char*>(frame->arguments[0]), static_cast<Size>(frame->arguments[1]));

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    /**
     * Provide one small compatibility surface for legacy module-style GUI input
     * injection used by bring-up helpers such as the shell demo commands.
     *
     * @param frame Active syscall frame.
     * @return StatusOK on success, or a negative kernel status code.
     */
    Status service_module_invoke(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        char module_name[ServicePathCapacity];
        char export_name[ServiceTextCapacity];
        long module_result = static_cast<long>(StatusNotSupported);
        long* user_result = reinterpret_cast<long*>(frame->arguments[4]);
        Status status;

        if ((process == NULL) || (user_result == NULL) || !user_range_valid(user_result, sizeof(*user_result))) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), module_name, sizeof(module_name));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[1]), export_name, sizeof(export_name));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        if (same_text_case_insensitive(module_name, "fbgui") && same_text_case_insensitive(export_name, "push_key")) {
            const U32 key = static_cast<U32>(frame->arguments[2] & 0xFFFFFFFFU);

            GuiService::publish_input_event(ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN, 0U, 0U, key, 0U);
            GuiService::publish_input_event(ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP, 0U, 0U, key, 0U);
            module_result = 0L;
            status = StatusOK;
        }
        else if (same_text_case_insensitive(module_name, "fbgui") && same_text_case_insensitive(export_name, "push_pointer")) {
            const U32 x = static_cast<U32>(frame->arguments[2] & 0xFFFFFFFFU);
            const U32 y = static_cast<U32>(frame->arguments[3] & 0xFFFFFFFFU);
            const U32 type = (x == static_cast<U32>(ROS_KERNEL_GUI_POINTER_HIDDEN) && y == static_cast<U32>(ROS_KERNEL_GUI_POINTER_HIDDEN))
                ? ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE
                : ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE;

            GuiService::publish_input_event(type, x, y, 0U, 0U);
            module_result = 0L;
            status = StatusOK;
        }
        else {
            status = StatusNotSupported;
        }

        if (status == StatusOK) {
            status = copy_kernel_bytes_to_user(user_result, &module_result, sizeof(module_result));
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    /**
     * Handle the framebuffer-present and shared window-surface GUI ABI.
     *
     * The live userspace GUI stack needs two capabilities from the kernel:
     * presenting damaged desktop rectangles to the hardware framebuffer and
     * mapping one shared backing surface into both GWES and the owning client.
     *
     * @param frame Active syscall frame.
     * @return StatusOK on success, or the propagated GUI service failure code.
     */
    Status service_gui_control(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        const U64 command = frame->arguments[0];
        const U64 value = frame->arguments[1];
        Status status = StatusNotSupported;

        if ((frame == NULL) || (process == NULL)) {
            if (frame != NULL) {
                frame->result = encode_status(StatusInvalidArgument);
            }
            return StatusInvalidArgument;
        }

        switch (command) {
        case ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO: {
            RosKernelGuiDisplayInfo info = {};

            if (!user_range_valid(reinterpret_cast<void*>(value), sizeof(info))) {
                frame->result = encode_status(StatusInvalidArgument);
                return StatusInvalidArgument;
            }

            status = GuiService::query_display_info(&info);
            if (status == StatusOK) {
                status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(value), &info, sizeof(info));
            }
            break;
        }
        case ROS_KERNEL_GUI_CONTROL_DISPLAY_PRESENT: {
            RosKernelGuiPresentBuffer buffer = {};
            Size span;

            if (!user_range_valid(reinterpret_cast<void*>(value), sizeof(buffer))) {
                frame->result = encode_status(StatusInvalidArgument);
                return StatusInvalidArgument;
            }

            status = copy_user_bytes_to_kernel(reinterpret_cast<const void*>(value), &buffer, sizeof(buffer));
            if (status != StatusOK) {
                break;
            }

            span = gui_present_user_span(&buffer);
            if ((span == 0U) || !user_range_valid(reinterpret_cast<const void*>(static_cast<Uptr>(buffer.pixels)), span)) {
                status = StatusInvalidArgument;
                break;
            }

            status = GuiService::present_buffer(&buffer);
            break;
        }
        case ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE:
        case ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_QUERY: {
            RosKernelGuiSharedInputView view = {};

            if (!user_range_valid(reinterpret_cast<void*>(value), sizeof(view))) {
                frame->result = encode_status(StatusInvalidArgument);
                return StatusInvalidArgument;
            }

            if (command == ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE) {
                status = GuiService::acquire_shared_input(process, &view);
            }
            else {
                status = GuiService::query_shared_input(process, &view);
            }

            if (status == StatusOK) {
                status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(value), &view, sizeof(view));
            }
            break;
        }
        case ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_RELEASE:
            status = GuiService::release_shared_input(process);
            break;
        case ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_CREATE:
        case ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_ACQUIRE:
        case ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_DESTROY:
        case ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_RELEASE: {
            GuiWindowSurfaceView view = {};

            if (!user_range_valid(reinterpret_cast<void*>(value), sizeof(view))) {
                frame->result = encode_status(StatusInvalidArgument);
                return StatusInvalidArgument;
            }

            status = copy_user_bytes_to_kernel(reinterpret_cast<const void*>(value), &view, sizeof(view));
            if (status != StatusOK) {
                break;
            }
            if (view.version != ROS_KERNEL_GUI_WINDOW_SURFACE_VIEW_VERSION) {
                status = StatusInvalidArgument;
                break;
            }

            if (command == ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_CREATE) {
                status = GuiService::create_window_surface(process, &view);
            }
            else if (command == ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_ACQUIRE) {
                status = GuiService::acquire_window_surface(process, &view);
            }
            else if (command == ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_DESTROY) {
                status = GuiService::destroy_window_surface(process, &view);
            }
            else {
                status = GuiService::release_window_surface(process, &view);
            }

            if ((status == StatusOK)
                && ((command == ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_CREATE)
                    || (command == ROS_KERNEL_GUI_CONTROL_WINDOW_SURFACE_ACQUIRE))) {
                status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(value), &view, sizeof(view));
            }
            break;
        }
        default:
            status = StatusNotSupported;
            break;
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_shlib_open(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        char module_path[ServicePathCapacity];
        VirtAddr module_handle = 0U;
        bool needs_process_attach = false;
        Status status;

        if (process == NULL) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), module_path, sizeof(module_path));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        status = DllLoader::load_library(process, module_path, &module_handle, &needs_process_attach);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        frame->result = needs_process_attach ? (module_handle | UserLoaderActionBit) : module_handle;
        return StatusOK;
    }

    Status service_shlib_export(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        char module_path[ServicePathCapacity];
        char export_name[ServiceTextCapacity];
        VirtAddr module_handle = 0U;
        VirtAddr export_address = 0U;
        Status status;

        if (process == NULL) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        if (DllLoader::is_module_handle(process, frame->arguments[0])) {
            module_handle = frame->arguments[0];
        }
        else {
            status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), module_path, sizeof(module_path));
            if (status != StatusOK) {
                frame->result = encode_status(status);
                return status;
            }
        }

        status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[1]), export_name, sizeof(export_name));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        status = DllLoader::get_proc_address(
            process,
            module_handle,
            module_handle != 0U ? NULL : module_path,
            export_name,
            &export_address);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        frame->result = export_address;
        return StatusOK;
    }

    Status service_shlib_close(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        char module_path[ServicePathCapacity];
        VirtAddr module_handle = 0U;
        VirtAddr action_handle = 0U;
        Status status;

        if (process == NULL) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        if (DllLoader::is_module_handle(process, frame->arguments[0])) {
            module_handle = frame->arguments[0];
        }
        else {
            status = copy_user_string(reinterpret_cast<const char*>(frame->arguments[0]), module_path, sizeof(module_path));
            if (status != StatusOK) {
                frame->result = encode_status(status);
                return status;
            }
        }

        status = DllLoader::free_library(
            process,
            module_handle,
            module_handle != 0U ? NULL : module_path,
            &action_handle);
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        frame->result = (action_handle != 0U) ? (action_handle | UserLoaderActionBit) : 0U;
        return StatusOK;
    }

    Status service_driver_unload(ServiceFrame* frame) {
        return service_shlib_close(frame);
    }

    Status service_event_subscribe(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        KernelEventSubscriptionRequest request;
        KernelEventSubscriptionId subscription_id = 0ULL;
        Status status;

        if ((process == NULL) || (frame->arguments[0] == 0U) || (frame->arguments[1] == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_user_bytes_to_kernel(
            reinterpret_cast<const void*>(frame->arguments[0]),
            &request,
            sizeof(request));
        if (status == StatusOK) {
            status = KernelEventBroker::subscribe(process->id, &request, &subscription_id);
        }
        if (status == StatusOK) {
            status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[1]), &subscription_id, sizeof(subscription_id));
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_event_read(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        KernelEventRecord records[KERNEL_EVENT_MAX_READ_BATCH];
        KernelEventSubscriptionId subscription_id = frame->arguments[0];
        KernelEventRecord* user_records = reinterpret_cast<KernelEventRecord*>(frame->arguments[1]);
        U32 capacity = static_cast<U32>(frame->arguments[2]);
        U32 count = 0U;
        Status status;

        if ((process == NULL) || (frame->arguments[3] == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }
        if (capacity > KERNEL_EVENT_MAX_READ_BATCH) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }
        if ((capacity != 0U) && !user_range_valid(user_records, static_cast<Size>(capacity) * sizeof(KernelEventRecord))) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = KernelEventBroker::read(process->id, subscription_id, records, capacity, &count);
        if ((status == StatusOK) && (count != 0U)) {
            status = copy_kernel_bytes_to_user(user_records, records, static_cast<Size>(count) * sizeof(KernelEventRecord));
        }
        if (status == StatusOK) {
            status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[3]), &count, sizeof(count));
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_event_query(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        KernelEventSubscriptionInfo info;
        Status status;

        if ((process == NULL) || (frame->arguments[1] == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = KernelEventBroker::query(process->id, frame->arguments[0], &info);
        if (status == StatusOK) {
            status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[1]), &info, sizeof(info));
        }

        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_event_unsubscribe(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        Status status;

        if (process == NULL) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = KernelEventBroker::unsubscribe(process->id, frame->arguments[0]);
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_event_wait(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        Status status;

        if ((process == NULL) || (frame->arguments[0] == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = KernelEventBroker::wait(process->id, frame->arguments[0]);
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    Status service_uptime(ServiceFrame* frame) {
        frame->result = KernelTime::ticks_to_milliseconds(Scheduler::tick_count());
        return StatusOK;
    }

    Status service_log_write(ServiceFrame* frame) {
        return service_write(frame);
    }

    Status service_log_recv(ServiceFrame* frame) {
        char* user_buffer = reinterpret_cast<char*>(frame->arguments[0]);
        Size size = static_cast<Size>(frame->arguments[1]);

        if ((size == 0U) || !user_range_valid(user_buffer, size)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        user_buffer[0] = '\0';
        frame->result = encode_status(StatusNotSupported);
        return StatusNotSupported;
    }

    /**
     * Copy one user packet into the broker and route it to the destination
     * process identified by the syscall argument.
     *
     * @param frame Active syscall frame.
     * @return StatusOK on success, or a negative kernel status code.
     */
    Status service_ipc_send(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        UserIpcMessage message;
        Status status;

        if ((process == NULL) || (frame->arguments[0] == 0U) || (frame->arguments[1] == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        status = copy_user_bytes_to_kernel(reinterpret_cast<const void*>(frame->arguments[1]), &message, sizeof(message));
        if (status != StatusOK) {
            frame->result = encode_status(status);
            return status;
        }

        ipc_sweep_dead_queues();
        message.sender_pid = static_cast<long>(process->id);
        message.receiver_pid = static_cast<long>(frame->arguments[0]);
        status = ipc_enqueue(frame->arguments[0], &message);
        frame->result = (status == StatusOK) ? 0U : encode_status(status);
        return status;
    }

    /**
     * Receive one queued packet for the current process.
     *
     * `ROS_USER_IPC_RECEIVE_WAIT` blocks the current thread with wait-reason
     * `Message` until a sender routes a packet here.
     *
     * @param frame Active syscall frame.
     * @return StatusOK on success, or a negative kernel status code.
     */
    Status service_ipc_recv(ServiceFrame* frame) {
        Process* process = Scheduler::current_process();
        UserIpcMessage message;
        const unsigned long flags = frame->arguments[1];
        const unsigned long timeout_msec = ROS_USER_IPC_RECEIVE_TIMEOUT_MSEC(flags);
        Status status;

        if ((process == NULL) || (frame->arguments[0] == 0U)) {
            frame->result = encode_status(StatusInvalidArgument);
            return StatusInvalidArgument;
        }

        for (;;) {
            ipc_sweep_dead_queues();
            status = ipc_dequeue(process->id, &message);
            if (status == StatusOK) {
                status = copy_kernel_bytes_to_user(reinterpret_cast<void*>(frame->arguments[0]), &message, sizeof(message));
                frame->result = (status == StatusOK) ? 0U : encode_status(status);
                return status;
            }
            if ((status != StatusBusy) || ((flags & ROS_USER_IPC_RECEIVE_WAIT) == 0U)) {
                frame->result = encode_status(status);
                return status;
            }

            if (timeout_msec != 0UL) {
                status = Scheduler::block_current_for(
                    WaitReason::Message,
                    KernelTime::milliseconds_to_ticks_ceil(timeout_msec));
            }
            else {
                status = Scheduler::block_current(WaitReason::Message);
            }
            if (status != StatusOK) {
                frame->result = encode_status(status);
                return status;
            }
        }
    }

    ServiceHandler g_service_handlers[SyscallCount] = {
        &service_write,
        &service_malloc,
        &service_free,
        &service_unsupported,
        &service_exit,
        &service_get_param,
        &service_sleep,
        &service_unsupported,
        &service_shlib_open,
        &service_task_name,
        &service_spawn,
        &service_shared_memory_acquire,
        &service_console_read,
        &service_read_file,
        &service_directory_entry,
        &service_mkdir,
        &service_task_info,
        &service_mem_info,
        &service_unsupported,
        &service_reboot,
        &service_debug_shell,
        &service_unsupported,
        &service_task_args,
        &service_module_invoke,
        &service_shlib_export,
        &service_shlib_close,
        &service_driver_unload,
        &service_wait_pid,
        &service_gui_control,
        &service_unsupported,
        &service_uptime,
        &service_log_write,
        &service_log_recv,
        &service_log_write,
        &service_event_subscribe,
        &service_event_read,
        &service_event_query,
        &service_event_unsubscribe,
        &service_event_wait,
        &service_remove,
        &service_ipc_send,
        &service_ipc_recv,
    };

} // namespace

Status service_dispatch(ServiceFrame* frame) {
    ServiceHandler handler;

    if (frame == NULL) {
        return StatusInvalidArgument;
    }

    // User tasks can finish on one syscall and leave their thread parked on
    // the retired list until some later safe kernel entry destroys it. Reap
    // before dispatching the next syscall so short-lived tools do not pile up
    // as lingering exiting processes.
    Scheduler::reap_retired();

    if (frame->service_id >= SyscallCount) {
        frame->result = encode_status(StatusNotSupported);
        frame->status = StatusNotSupported;
        return frame->status;
    }

    handler = g_service_handlers[frame->service_id];
    if (handler == NULL) {
        frame->result = encode_status(StatusNotSupported);
        frame->status = StatusNotSupported;
        return frame->status;
    }

    frame->status = handler(frame);
    return frame->status;
}

extern "C" U64 aarch64_dispatch_service(
    U64 service_id,
    U64 arg0,
    U64 arg1,
    U64 arg2,
    U64 arg3,
    U64 arg4,
    U64 arg5,
    AArch64ExceptionFrame* trap_frame) {
    ServiceFrame frame = {};

    frame.service_id = service_id;
    frame.arguments[0] = arg0;
    frame.arguments[1] = arg1;
    frame.arguments[2] = arg2;
    frame.arguments[3] = arg3;
    frame.arguments[4] = arg4;
    frame.arguments[5] = arg5;
    frame.trap_frame = trap_frame;
    (void)service_dispatch(&frame);
    return frame.result;
}