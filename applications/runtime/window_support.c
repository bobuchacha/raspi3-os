#define ROS_WINDOWKIT_EXPORTS 1
#include "app/syscall.h"
#include "app/window.h"

DLL_EXPORT(CreateWindowClass);
DLL_EXPORT(CreateWindowEx);
DLL_EXPORT(CreateWindow);
DLL_EXPORT(SetWindowCursor);
DLL_EXPORT(MoveWindow);
DLL_EXPORT(SendMessage);
DLL_EXPORT(PostMessage);
DLL_EXPORT(PostQuitMessage);
DLL_EXPORT(GetMessage);
DLL_EXPORT(TranslateMessage);
DLL_EXPORT(DispatchMessage);
DLL_EXPORT(WindowMessageName);

#define WINDOW_SERVER_WAIT_MSEC 100UL
#define WINDOW_SERVER_WAIT_RETRIES 50UL
#define WINDOW_REPLY_WAIT_MSEC 250UL
#define WINDOW_REPLY_LOG_INTERVAL_MSEC 2000UL

typedef struct WindowClassRegistration {
    char name[ROS_WINDOW_CLASS_NAME_MAX];
    WNDPROC proc;
    struct WindowClassRegistration* next;
} WindowClassRegistration;

typedef struct WindowHandleRegistration {
    HWND hwnd;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    WNDPROC proc;
    struct WindowHandleRegistration* next;
} WindowHandleRegistration;

typedef struct WindowQueuedMessage {
    MSG message;
    struct WindowQueuedMessage* next;
    struct WindowQueuedMessage* prev;
} WindowQueuedMessage;

static WindowClassRegistration* g_window_classes = 0;
static WindowHandleRegistration* g_window_handles = 0;
static WindowQueuedMessage* g_window_queue_head = 0;
static WindowQueuedMessage* g_window_queue_tail = 0;
static unsigned long g_window_class_count = 0UL;
static unsigned long g_window_class_peak = 0UL;
static unsigned long g_window_handle_count = 0UL;
static unsigned long g_window_handle_peak = 0UL;
static unsigned long g_window_queue_count = 0UL;
static unsigned long g_window_queue_peak = 0UL;
static long g_window_gwes_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
static int g_window_quit_notified = 0;

static void window_log_reply_wait(unsigned long expected_kind, unsigned long waited_msec);

typedef enum WindowQueueEnqueueResult {
    WINDOW_QUEUE_ENQUEUED = 0,
    WINDOW_QUEUE_IGNORED = 1
} WindowQueueEnqueueResult;

/*
 * window_entry
 *
 * The dedicated client DLL owns process-local class and queue state, so the
 * attach path resets everything for the new process image and the detach path
 * drops any stale state before the loader unmaps the module.
 *
 * @param image_base Base address where the DLL is mapped in the current process.
 * @param reason Loader notification reason.
 * @return Non-zero success code for the loader.
 */
int window_entry(void* image_base, U32 reason);
static void window_zero_memory(void* destination, unsigned long size);
static int window_pid_is_live(long pid);

/*
 * Write one compact reply-wait diagnostic line.
 *
 * Hanging forever inside `CreateWindowClass` or `CreateWindowEx` makes GUI app
 * failures look like launcher regressions. A periodic line here tells us
 * whether the client is waiting on GWES, how long it has been waiting, and
 * whether the cached GWES PID still resolves.
 *
 * @param expected_kind Window-server reply kind the caller is awaiting.
 * @param waited_msec Total elapsed wait time in milliseconds.
 * @return Nothing.
 */
static void window_log_reply_wait(unsigned long expected_kind, unsigned long waited_msec) {
    (void)expected_kind;
    (void)waited_msec;
    writeLine("window.dll: waiting for gwes reply");
}

/*
 * Allocate one window-runtime heap block directly from the kernel-backed
 * userspace allocator.
 *
 * window.dll is a thin helper DLL and does not own a hosted libc allocator.
 * Using the raw syscall-backed heap keeps the new dynamic registries local to
 * the DLL without introducing any extra shared-library dependency.
 *
 * @param size Byte count to allocate.
 * @return Heap block on success, or null on failure.
 */
static void* window_heap_alloc(unsigned long size) {
    unsigned long bytes = size != 0UL ? size : 1UL;
    long result = (long)invokeSyscall1(SYS_MALLOC, bytes);

    return result < 0L ? 0 : (void*)(unsigned long)result;
}

/*
 * Release one window-runtime heap block acquired from `window_heap_alloc`.
 *
 * @param memory Heap block to free.
 * @return Nothing.
 */
static void window_heap_free(void* memory) {
    if (!memory) {
        return;
    }

    (void)invokeSyscall1(SYS_FREE, (unsigned long)memory);
}

/*
 * Copy one string into a fixed-size destination and always terminate it.
 *
 * @param destination Output buffer.
 * @param size Output buffer size.
 * @param text Optional source string.
 * @return Nothing.
 */
static void window_copy_text(char* destination, unsigned long size, const char* text) {
    unsigned long index = 0UL;

    if (!destination || size == 0UL) {
        return;
    }
    if (!text) {
        destination[0] = '\0';
        return;
    }

    while (index + 1UL < size && text[index] != '\0') {
        destination[index] = text[index];
        ++index;
    }
    destination[index] = '\0';
}

/*
 * Link one class registration into the local per-process registry.
 *
 * @param slot Newly initialized class record.
 * @return Nothing.
 */
static void window_link_class(WindowClassRegistration* slot) {
    if (!slot) {
        return;
    }

    slot->next = g_window_classes;
    g_window_classes = slot;
    ++g_window_class_count;
    if (g_window_class_count > g_window_class_peak) {
        g_window_class_peak = g_window_class_count;
    }
}

/*
 * Link one live window handle into the local per-process registry.
 *
 * @param slot Newly initialized handle record.
 * @return Nothing.
 */
static void window_link_handle(WindowHandleRegistration* slot) {
    if (!slot) {
        return;
    }

    slot->next = g_window_handles;
    g_window_handles = slot;
    ++g_window_handle_count;
    if (g_window_handle_count > g_window_handle_peak) {
        g_window_handle_peak = g_window_handle_count;
    }
}

/*
 * Release every class registration owned by the current process.
 *
 * @return Nothing.
 */
static void window_release_all_classes(void) {
    WindowClassRegistration* current = g_window_classes;

    while (current != 0) {
        WindowClassRegistration* next = current->next;

        window_heap_free(current);
        current = next;
    }

    g_window_classes = 0;
    g_window_class_count = 0UL;
}

/*
 * Release every handle registration owned by the current process.
 *
 * @return Nothing.
 */
static void window_release_all_handles(void) {
    WindowHandleRegistration* current = g_window_handles;

    while (current != 0) {
        WindowHandleRegistration* next = current->next;

        window_heap_free(current);
        current = next;
    }

    g_window_handles = 0;
    g_window_handle_count = 0UL;
}

/*
 * Release every queued local message owned by the current process.
 *
 * @return Nothing.
 */
static void window_release_all_messages(void) {
    WindowQueuedMessage* current = g_window_queue_head;

    while (current != 0) {
        WindowQueuedMessage* next = current->next;

        window_heap_free(current);
        current = next;
    }

    g_window_queue_head = 0;
    g_window_queue_tail = 0;
    g_window_queue_count = 0UL;
}

/*
 * Pack two 32-bit values into one IPC scalar.
 *
 * The window protocol uses four scalar fields, so creation requests collapse
 * X/Y and width/height pairs to keep the wire format stable while still
 * carrying parent-child placement metadata.
 *
 * @param first Upper 32-bit payload.
 * @param second Lower 32-bit payload.
 * @return Packed 64-bit scalar.
 */
static unsigned long window_pack_pair(unsigned long first, unsigned long second) {
    return ((first & 0xFFFFFFFFUL) << 32) | (second & 0xFFFFFFFFUL);
}

/*
 * Find one registered local class by name.
 *
 * @param class_name Exact class name.
 * @return Matching class slot, or NULL when the class is unknown.
 */
static WindowClassRegistration* window_find_class(const char* class_name) {
    WindowClassRegistration* current;

    for (current = g_window_classes; current != 0; current = current->next) {
        if (userIpcTextEquals(current->name, class_name)) {
            return current;
        }
    }

    return NULL;
}

/*
 * Allocate one heap-backed class registration record.
 *
 * @return Fresh record on success, or NULL when allocation fails.
 */
static WindowClassRegistration* window_reserve_class(void) {
    return (WindowClassRegistration*)window_heap_alloc(sizeof(WindowClassRegistration));
}

/*
 * Find one local window handle registration.
 *
 * @param hwnd Window handle.
 * @return Matching handle slot, or NULL when the handle is unknown.
 */
static WindowHandleRegistration* window_find_handle(HWND hwnd) {
    WindowHandleRegistration* current;

    for (current = g_window_handles; current != 0; current = current->next) {
        if (current->hwnd == hwnd) {
            return current;
        }
    }

    return NULL;
}

/*
 * Allocate one heap-backed window-handle record.
 *
 * @return Fresh record on success, or NULL when allocation fails.
 */
static WindowHandleRegistration* window_reserve_handle(void) {
    return (WindowHandleRegistration*)window_heap_alloc(sizeof(WindowHandleRegistration));
}

/*
 * Check whether one cached PID still resolves to a live process.
 *
 * @param pid Process identifier to validate.
 * @return Non-zero when the PID is still live.
 */
static int window_pid_is_live(long pid) {
    UserTaskInfo info;

    if (pid < 0) {
        return 0;
    }
    if (getTaskInfo(pid, &info) < 0) {
        return 0;
    }

    return info.main_thread_state != USER_TASK_STATE_TERMINATED;
}

/*
 * Resolve and cache the GWES PID.
 *
 * @return GWES PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static long window_resolve_gwes_pid(void) {
    long published_pid;

    if (g_window_gwes_pid >= 0) {
        return g_window_gwes_pid;
    }

    published_pid = WindowServerPublishedPid();
    if (published_pid >= 0) {
        g_window_gwes_pid = published_pid;
        return g_window_gwes_pid;
    }

    g_window_gwes_pid = findUserTaskPidByName(ROS_WINDOW_SERVER_NAME);
    return g_window_gwes_pid;
}

/*
 * Wait for the boot supervisor to launch GWES.
 *
 * @return GWES PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static long window_wait_for_gwes_pid(void) {
    unsigned long attempt;

    for (attempt = 0UL; attempt < WINDOW_SERVER_WAIT_RETRIES; ++attempt) {
        long pid = window_resolve_gwes_pid();

        if (pid >= 0) {
            writeLine("window.dll: found gwes");
            return pid;
        }

        if (attempt == 0UL || ((attempt + 1UL) % 10UL) == 0UL) {
            writeLine("window.dll: waiting for gwes pid");
        }

        (void)sleepMs(WINDOW_SERVER_WAIT_MSEC);
    }

    writeLine("window.dll: gwes pid lookup failed");
    return ROS_USER_IPC_STATUS_NOT_FOUND;
}

/*
 * Push one message into the local per-process queue.
 *
 * @param message Message to queue.
 * @return Zero on success, or `ROS_USER_IPC_STATUS_NO_SPACE`.
 */
static long window_queue_push_raw(const MSG* message) {
    WindowQueuedMessage* node;

    if (!message) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    node = (WindowQueuedMessage*)window_heap_alloc(sizeof(WindowQueuedMessage));
    if (!node) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    window_zero_memory(node, sizeof(*node));
    node->message = *message;
    node->prev = g_window_queue_tail;
    if (g_window_queue_tail != 0) {
        g_window_queue_tail->next = node;
    }
    else {
        g_window_queue_head = node;
    }

    g_window_queue_tail = node;
    ++g_window_queue_count;
    if (g_window_queue_count > g_window_queue_peak) {
        g_window_queue_peak = g_window_queue_count;
    }
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Return whether one posted message should replace older queued instances.
 *
 * Win32 coalesces several high-frequency queue-driven messages so pointer
 * motion and invalidation bursts do not overwhelm the UI thread. Keeping that
 * policy in one helper makes the async path predictable and lets the message
 * pump stay responsive while the pointer moves across a busy window.
 *
 * @param message Window message identifier.
 * @return Non-zero when older queued instances should be collapsed.
 */
static int window_message_should_coalesce(unsigned long message) {
    return message == WM_MOUSEMOVE
        || message == WM_PAINT
        || message == WM_TIMER
        || message == WM_MOVE
        || message == WM_SIZE;
}

/*
 * Try to fold one high-frequency message into an older queued instance.
 *
 * The newest payload wins so callers observe the most recent pointer position,
 * paint request, or geometry update without paying to process stale entries.
 *
 * @param message Message being enqueued.
 * @return Non-zero when an existing queue slot was updated in place.
 */
static int window_queue_coalesce_existing(const MSG* message) {
    WindowQueuedMessage* queued;

    if (!message || !window_message_should_coalesce(message->message) || g_window_queue_count == 0UL) {
        return 0;
    }

    for (queued = g_window_queue_tail; queued != 0; queued = queued->prev) {
        if (queued->message.hwnd != message->hwnd || queued->message.message != message->message) {
            continue;
        }

        queued->message = *message;
        return 1;
    }

    return 0;
}

/*
 * Push one message into the local queue using Win32-style post semantics.
 *
 * High-frequency posted messages are coalesced so the app thread sees the most
 * recent state instead of every transient mouse position and repaint request.
 *
 * @param message Message to queue.
 * @return Zero on success, or `ROS_USER_IPC_STATUS_NO_SPACE` when the queue is full.
 */
static long window_queue_enqueue(const MSG* message) {
    if (window_queue_coalesce_existing(message)) {
        return ROS_USER_IPC_STATUS_OK;
    }

    return window_queue_push_raw(message);
}

/*
 * Fill one caller-owned byte range with zero without relying on a hosted libc
 * symbol.
 *
 * The window client DLL must stay self-contained at load time, otherwise the
 * user-image builder will emit an unintended import for `memset` from the
 * synthetic `kernel` module and the loader will reject the image.
 *
 * @param destination Buffer to clear.
 * @param size Number of bytes to clear.
 * @return Nothing.
 */
static void window_zero_memory(void* destination, unsigned long size) {
    unsigned char* bytes = (unsigned char*)destination;
    unsigned long index;

    if (!bytes) {
        return;
    }

    for (index = 0UL; index < size; ++index) {
        bytes[index] = 0U;
    }
}

/*
 * Reset all process-local client state owned by the window helper DLL.
 *
 * @return Nothing.
 */
static void window_reset_state(void) {
    window_release_all_classes();
    window_release_all_handles();
    window_release_all_messages();
    g_window_class_peak = 0UL;
    g_window_handle_peak = 0UL;
    g_window_queue_peak = 0UL;
    g_window_gwes_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
    g_window_quit_notified = 0;
}

/*
 * Pop one message from the local per-process queue.
 *
 * @param message Receives the next queued message.
 * @return One when a message was copied, or zero when the queue is empty.
 */
static int window_queue_pop(MSG* message) {
    WindowQueuedMessage* queued;

    if (!message || g_window_queue_count == 0UL) {
        return 0;
    }

    queued = g_window_queue_head;
    *message = queued->message;
    g_window_queue_head = queued->next;
    if (g_window_queue_head != 0) {
        g_window_queue_head->prev = 0;
    }
    else {
        g_window_queue_tail = 0;
    }
    --g_window_queue_count;
    window_heap_free(queued);
    return 1;
}

/*
 * Convert one GWES delivery packet into a local window message.
 *
 * @param packet Server delivery packet.
 * @param message Receives the converted message.
 * @return Non-zero on success.
 */
static int window_packet_to_message(const UserIpcMessage* packet, MSG* message) {
    if (!packet || !message) {
        return 0;
    }
    if (packet->protocol != ROS_WINDOW_SERVER_PROTOCOL || packet->kind != ROS_WINDOW_SERVER_KIND_DELIVER_MESSAGE) {
        return 0;
    }

    message->hwnd = (HWND)packet->arg0;
    message->message = packet->arg1;
    message->wParam = packet->arg2;
    message->lParam = packet->arg3;
    return 1;
}

/*
 * Convert one GWES delivery packet and queue it with post semantics.
 *
 * @param packet Server delivery packet.
 * @return `WINDOW_QUEUE_ENQUEUED` when the packet became a local message,
 * `WINDOW_QUEUE_IGNORED` when the packet was not a normal delivery, or a
 * negative status code on queue failure.
 */
static long window_enqueue_server_packet(const UserIpcMessage* packet) {
    MSG queued_message;

    if (!window_packet_to_message(packet, &queued_message)) {
        return WINDOW_QUEUE_IGNORED;
    }

    return window_queue_enqueue(&queued_message);
}

/*
 * Pull any immediately available GWES deliveries into the local post queue.
 *
 * Blocking receives happen elsewhere. This helper only drains packets that are
 * already pending so bursty mouse-move and paint traffic can be coalesced
 * before `GetMessage` returns the next item to the app thread.
 *
 * @return Zero on success, or a negative status code on queue failure.
 */
static long window_drain_pending_packets(void) {
    UserIpcMessage packet;
    long status;

    for (;;) {
        status = receiveUserIpcMessage(&packet, 0UL);
        if (status == ROS_USER_IPC_STATUS_BUSY) {
            return ROS_USER_IPC_STATUS_OK;
        }
        if (status < 0L) {
            return status;
        }

        status = window_enqueue_server_packet(&packet);
        if (status < 0L) {
            return status;
        }
    }
}

/*
 * Receive packets until the expected GWES reply arrives, queueing any normal
 * window deliveries that race with the synchronous round trip.
 *
 * @param expected_kind Reply kind the caller is waiting for.
 * @param packet Receives the reply packet.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_receive_reply(unsigned long expected_kind, UserIpcMessage* packet) {
    long status;
    unsigned long waited_msec = 0UL;

    if (!packet) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    for (;;) {
        status = receiveUserIpcMessage(
            packet,
            ROS_USER_IPC_RECEIVE_WAIT | ROS_USER_IPC_RECEIVE_TIMEOUT_ENCODE(WINDOW_REPLY_WAIT_MSEC));
        if (status == ROS_USER_IPC_STATUS_BUSY) {
            waited_msec += WINDOW_REPLY_WAIT_MSEC;
            if ((waited_msec % WINDOW_REPLY_LOG_INTERVAL_MSEC) == 0UL) {
                window_log_reply_wait(expected_kind, waited_msec);
            }
            if (!window_pid_is_live(g_window_gwes_pid)) {
                writeLine("window.dll: gwes disappeared while waiting for reply");
                return ROS_USER_IPC_STATUS_NOT_FOUND;
            }
            continue;
        }
        if (status < 0) {
            return status;
        }
        if (packet->protocol != ROS_WINDOW_SERVER_PROTOCOL) {
            continue;
        }
        if (packet->kind == expected_kind) {
            return ROS_USER_IPC_STATUS_OK;
        }
        status = window_enqueue_server_packet(packet);
        if (status < 0L) {
            return status;
        }
    }
}

/*
 * Send one request packet to GWES.
 *
 * @param packet Fully populated request packet.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_send_request(UserIpcMessage* packet) {
    long gwes_pid;

    if (!packet) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gwes_pid = window_wait_for_gwes_pid();
    if (gwes_pid < 0) {
        return gwes_pid;
    }

    packet->receiver_pid = gwes_pid;
    writeLine("window.dll: sending request to gwes");
    return sendUserIpcMessage(gwes_pid, packet);
}

/*
 * Notify GWES that the current client process is beginning its quit path.
 *
 * @param exit_code Application-supplied quit code copied into the server log.
 * @return Zero on success, or a negative status code when GWES is unavailable.
 */
static long window_send_process_quit(long exit_code) {
    UserIpcMessage request;

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_PROCESS_QUIT;
    request.arg0 = (unsigned long)exit_code;
    return window_send_request(&request);
}

long CreateWindowClass(const char* class_name, WNDPROC proc) {
    WindowClassRegistration* slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (!class_name || class_name[0] == '\0' || !proc) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (window_find_class(class_name) != NULL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    slot = window_reserve_class();
    if (!slot) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }
    window_zero_memory(slot, sizeof(*slot));

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_REGISTER_CLASS;
    window_copy_text(request.text, sizeof(request.text), class_name);

    writeLine("window.dll: register class request");
    status = window_send_request(&request);
    if (status < 0) {
        window_heap_free(slot);
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_REGISTER_CLASS_ACK, &reply);
    if (status < 0) {
        window_heap_free(slot);
        return status;
    }
    if ((long)reply.arg0 < 0) {
        window_heap_free(slot);
        return (long)reply.arg0;
    }

    slot->proc = proc;
    window_copy_text(slot->name, sizeof(slot->name), class_name);
    window_link_class(slot);
    return ROS_USER_IPC_STATUS_OK;
}

HWND CreateWindow(const char* class_name, const char* title) {
    WindowCreateParams params;

    params.class_name = class_name;
    params.title = title;
    params.parent = 0UL;
    params.x = 0L;
    params.y = 0L;
    params.width = 0UL;
    params.height = 0UL;
    params.style = ROS_WINDOW_STYLE_VISIBLE;
    return CreateWindowEx(&params);
}

/*
 * Create one server-owned window with explicit parent, geometry, and style.
 *
 * @param params Caller-owned creation parameters.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND CreateWindowEx(const WindowCreateParams* params) {
    WindowClassRegistration* class_slot;
    WindowHandleRegistration* handle_slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (!params || !params->class_name || params->class_name[0] == '\0') {
        writeLine("window.dll: create failed stage=validate");
        return 0UL;
    }

    class_slot = window_find_class(params->class_name);
    if (!class_slot) {
        writeLine("window.dll: create failed stage=class-local-not-found");
        return 0UL;
    }

    handle_slot = window_reserve_handle();
    if (!handle_slot) {
        writeLine("window.dll: create failed stage=handle-slot");
        return 0UL;
    }
    window_zero_memory(handle_slot, sizeof(*handle_slot));

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_CREATE_WINDOW;
    request.arg0 = (unsigned long)params->parent;
    request.arg1 = window_pack_pair((unsigned long)(unsigned int)params->x, (unsigned long)(unsigned int)params->y);
    request.arg2 = window_pack_pair(params->width, params->height);
    request.arg3 = params->style;
    window_copy_text(request.text, sizeof(request.text), params->class_name);
    window_copy_text(request.text2, sizeof(request.text2), params->title);

    writeLine("window.dll: create window request");
    status = window_send_request(&request);
    if (status < 0) {
        window_heap_free(handle_slot);
        writeLine("window.dll: create failed stage=send-request");
        return 0UL;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY, &reply);
    if (status < 0) {
        window_heap_free(handle_slot);
        writeLine("window.dll: create failed stage=receive-reply");
        return 0UL;
    }
    if ((long)reply.arg1 < 0) {
        window_heap_free(handle_slot);
        writeLine("window.dll: create failed stage=server-reply");
        return 0UL;
    }
    handle_slot->hwnd = (HWND)reply.arg0;
    handle_slot->proc = class_slot->proc;
    window_copy_text(handle_slot->class_name, sizeof(handle_slot->class_name), params->class_name);
    window_link_handle(handle_slot);

    // Queue one initial paint after the create handshake so clients can use the
    // Win32-style WM_CREATE -> WM_PAINT sequence for both top-level and child windows.
    (void)PostMessage(handle_slot->hwnd, WM_PAINT, 0UL, 0UL);
    return handle_slot->hwnd;
}

/*
 * Publish one preferred cursor asset for a live GWES-owned window.
 *
 * The client DLL validates that the handle belongs to this process before it
 * asks GWES to mutate server-side window metadata. That keeps accidental cross-
 * process handle reuse from silently changing the wrong window.
 *
 * @param hwnd Stable window handle returned by `CreateWindowEx`.
 * @param cursor_path DOS-style `.cur32` asset path, or null to clear.
 * @return Zero on success, or a negative status code on failure.
 */
long SetWindowCursor(HWND hwnd, const char* cursor_path) {
    WindowHandleRegistration* handle_slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (hwnd == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    handle_slot = window_find_handle(hwnd);
    if (!handle_slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR;
    request.arg0 = (unsigned long)hwnd;
    window_copy_text(request.text, sizeof(request.text), cursor_path ? cursor_path : "");

    status = window_send_request(&request);
    if (status < 0) {
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR_REPLY, &reply);
    if (status < 0) {
        return status;
    }

    return (long)reply.arg0;
}

long MoveWindow(HWND hwnd, long x, long y, unsigned long width, unsigned long height) {
    WindowHandleRegistration* handle_slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (hwnd == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    handle_slot = window_find_handle(hwnd);
    if (!handle_slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS;
    request.arg0 = (unsigned long)hwnd;
    request.arg1 = WindowPackSignedPair(x, y);
    request.arg2 = window_pack_pair(width, height);

    status = window_send_request(&request);
    if (status < 0) {
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS_REPLY, &reply);
    if (status < 0) {
        return status;
    }

    return (long)reply.arg0;
}

LRESULT SendMessage(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    WindowHandleRegistration* handle_slot;

    if (hwnd == 0UL || message == WM_QUIT) {
        return 0L;
    }

    handle_slot = window_find_handle(hwnd);
    if (!handle_slot || !handle_slot->proc) {
        return 0L;
    }

    return handle_slot->proc(hwnd, message, wParam, lParam);
}

long PostMessage(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    MSG queued_message;

    queued_message.hwnd = hwnd;
    queued_message.message = message;
    queued_message.wParam = wParam;
    queued_message.lParam = lParam;
    return window_queue_enqueue(&queued_message);
}

long PostQuitMessage(long exit_code) {
    long status = ROS_USER_IPC_STATUS_OK;

    if (!g_window_quit_notified) {
        status = window_send_process_quit(exit_code);
        if (status >= 0) {
            g_window_quit_notified = 1;
        }
    }

    if (PostMessage(0UL, WM_QUIT, (unsigned long)exit_code, 0UL) < 0) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    return status;
}

long GetMessage(MSG* message) {
    UserIpcMessage packet;
    long status;

    if (!message) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (window_queue_pop(message)) {
        return message->message == WM_QUIT ? 0L : 1L;
    }

    for (;;) {
        status = receiveUserIpcMessage(&packet, ROS_USER_IPC_RECEIVE_WAIT);
        if (status < 0) {
            return status;
        }

        status = window_enqueue_server_packet(&packet);
        if (status < 0L) {
            return status;
        }
        if (status == WINDOW_QUEUE_IGNORED) {
            continue;
        }

        status = window_drain_pending_packets();
        if (status < 0L) {
            return status;
        }
        if (window_queue_pop(message)) {
            return message->message == WM_QUIT ? 0L : 1L;
        }
    }
}

long TranslateMessage(const MSG* message) {
    (void)message;
    return 0L;
}

LRESULT DispatchMessage(const MSG* message) {
    if (!message || message->message == WM_QUIT) {
        return 0L;
    }

    return SendMessage(message->hwnd, message->message, message->wParam, message->lParam);
}

const char* WindowMessageName(unsigned long message) {
    switch (message) {
    case WM_NULL:
        return "WM_NULL";
    case WM_CREATE:
        return "WM_CREATE";
    case WM_DESTROY:
        return "WM_DESTROY";
    case WM_REPAINT:
        return "WM_REPAINT";
    case WM_MOUSEMOVE:
        return "WM_MOUSEMOVE";
    case WM_MOUSELEAVE:
        return "WM_MOUSELEAVE";
    case WM_LBUTTONDOWN:
        return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP:
        return "WM_LBUTTONUP";
    case WM_MOUSECLICKED:
        return "WM_MOUSECLICKED";
    case WM_KEYDOWN:
        return "WM_KEYDOWN";
    case WM_KEYUP:
        return "WM_KEYUP";
    case WM_CLOSE:
        return "WM_CLOSE";
    case WM_MOVE:
        return "WM_MOVE";
    case WM_SIZE:
        return "WM_SIZE";
    case WM_CHANGED:
        return "WM_CHANGED";
    case WM_QUIT:
        return "WM_QUIT";
    case WM_TIMER:
        return "WM_TIMER";
    case WM_PAINT:
        return "WM_PAINT";
    default:
        if (message >= WM_USER) {
            return "WM_USER";
        }
        return "WM_UNKNOWN";
    }
}

int window_entry(void* image_base, U32 reason) {
    (void)image_base;

    if ((reason == DLL_REASON_PROCESS_ATTACH) || (reason == DLL_REASON_PROCESS_DETACH)) {
        window_reset_state();
        return 1;
    }

    return 1;
}
