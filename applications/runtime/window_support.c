#define ROS_BUILDING_WINDOW_DLL 1
#define ROS_WINDOWKIT_EXPORTS 1
#include "app/syscall.h"
#include "app/window.h"
#include "app/gdi.h"

DLL_EXPORT(CreateWindowClass);
DLL_EXPORT(CreateWindowEx);
DLL_EXPORT(CreateWindow);
DLL_EXPORT(DestroyWindow);
DLL_EXPORT(CreateMenu);
DLL_EXPORT(DestroyMenu);
DLL_EXPORT(AppendMenuItem);
DLL_EXPORT(AppendSubMenu);
DLL_EXPORT(AppendMenuSeparator);
DLL_EXPORT(TrackPopupMenu);
DLL_EXPORT(SetWindowCursor);
DLL_EXPORT(MoveWindow);
DLL_EXPORT(SetForegroundWindow);
DLL_EXPORT(PostSetForegroundWindow);
DLL_EXPORT(SetTimer);
DLL_EXPORT(KillTimer);
DLL_EXPORT(SendMessage);
DLL_EXPORT(PostMessage);
DLL_EXPORT(PostQuitMessage);
DLL_EXPORT(ReceiveProcessIpcMessage);
DLL_EXPORT(GetMessage);
DLL_EXPORT(TranslateMessage);
DLL_EXPORT(DispatchMessage);
DLL_EXPORT(MessageBoxShow);
DLL_EXPORT(MessageBox);
DLL_EXPORT(MessageBoxError);
DLL_EXPORT(LoadFileAssetAsync);
DLL_EXPORT(LoadFileAssetAsyncNotify);
DLL_EXPORT(LoadModuleSectionAssetAsync);
DLL_EXPORT(LoadModuleSectionAssetAsyncNotify);
DLL_EXPORT(ReceiveAssetCompletion);
DLL_EXPORT(FreeAssetBuffer);

#define WINDOW_SERVER_WAIT_MSEC 100UL
#define WINDOW_SERVER_WAIT_RETRIES 50UL
#define WINDOW_REPLY_WAIT_MSEC 250UL
#define WINDOW_REPLY_LOG_INTERVAL_MSEC 2000UL
#define WINDOW_STATUS_ERROR (-4L)
#define WINDOW_ASSET_WORKER_IDLE_SLEEP_MSEC 10UL
#define WINDOW_ASSET_FILE_CHUNK 4096UL
#define WINDOW_ASSET_SHUTDOWN_WAIT_MSEC 1000UL
#define WINDOW_MESSAGEBOX_CLASS_NAME "builtin.messagebox"
#define WINDOW_MESSAGEBOX_DEFAULT_TITLE "Notification"
#define WINDOW_MESSAGEBOX_DEFAULT_ERROR_TITLE "Error"
#define WINDOW_MESSAGEBOX_DEFAULT_WIDTH 360UL
#define WINDOW_MESSAGEBOX_DEFAULT_HEIGHT 168UL
#define WINDOW_MESSAGEBOX_MIN_MARGIN 16UL
#define WINDOW_MESSAGEBOX_TEXT_X 18UL
#define WINDOW_MESSAGEBOX_TEXT_Y 18UL
#define WINDOW_MESSAGEBOX_TEXT_W 324UL
#define WINDOW_MESSAGEBOX_TEXT_H 92UL
#define WINDOW_MESSAGEBOX_BUTTON_WIDTH 84UL
#define WINDOW_MESSAGEBOX_BUTTON_HEIGHT 28UL
#define WINDOW_MESSAGEBOX_BUTTON_Y 118UL
#define WINDOW_MESSAGEBOX_BG_NORMAL 0x00F3F1E8UL
#define WINDOW_MESSAGEBOX_BG_ERROR 0x00F7E4E1UL
#define WINDOW_MESSAGEBOX_PANEL_NORMAL 0x00FFFDF7UL
#define WINDOW_MESSAGEBOX_PANEL_ERROR 0x00FFF4F1UL
#define WINDOW_MESSAGEBOX_FRAME_NORMAL 0x00877D6DUL
#define WINDOW_MESSAGEBOX_FRAME_ERROR 0x00A05347UL
#define WINDOW_MESSAGEBOX_TEXT_NORMAL 0x00222222UL
#define WINDOW_MESSAGEBOX_TEXT_ERROR 0x00532018UL
#define WINDOW_MESSAGEBOX_BUTTON_TOP 0x00FEFEFCUL
#define WINDOW_MESSAGEBOX_BUTTON_BOTTOM 0x00D8E3F2UL
#define WINDOW_MESSAGEBOX_BUTTON_PRESSED 0x00C9D3E0UL
#define WINDOW_MESSAGEBOX_BUTTON_FRAME 0x00796F60UL

typedef struct WindowMessageBoxState {
    HWND hwnd;
    HWND owner;
    const char* title;
    const char* message;
    unsigned long flags;
    unsigned long width;
    unsigned long height;
    int pointer_down_ok;
    int result_ready;
    long result;
} WindowMessageBoxState;

typedef struct WindowClassRegistration {
    char name[ROS_WINDOW_CLASS_NAME_MAX];
    WNDPROC proc;
    struct WindowClassRegistration* next;
} WindowClassRegistration;

typedef struct WindowHandleRegistration {
    HWND hwnd;
    HWND parent;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    WNDPROC proc;
    long x;
    long y;
    unsigned long width;
    unsigned long height;
    unsigned long style;
    struct WindowHandleRegistration* next;
} WindowHandleRegistration;

typedef struct WindowLocalMenuItem {
    unsigned long command_id;
    unsigned long flags;
    unsigned long hotkey;
    HMENU submenu_handle;
    char text[ROS_WINDOW_MENU_ITEM_TEXT_MAX];
} WindowLocalMenuItem;

typedef struct WindowMenuRegistration {
    HMENU handle;
    unsigned long item_count;
    WindowLocalMenuItem items[ROS_WINDOW_MENU_MAX_ITEMS];
    struct WindowMenuRegistration* next;
} WindowMenuRegistration;

typedef struct WindowEncodedMenuMap {
    HMENU handle;
    unsigned long menu_index;
} WindowEncodedMenuMap;

typedef struct WindowQueuedMessage {
    MSG message;
    struct WindowQueuedMessage* next;
    struct WindowQueuedMessage* prev;
} WindowQueuedMessage;

typedef struct WindowQueuedPacket {
    UserIpcMessage packet;
    struct WindowQueuedPacket* next;
    struct WindowQueuedPacket* prev;
} WindowQueuedPacket;

typedef enum WindowAsyncAssetRequestKind {
    WINDOW_ASSET_REQUEST_KIND_FILE = 1,
    WINDOW_ASSET_REQUEST_KIND_MODULE_SECTION = 2
} WindowAsyncAssetRequestKind;

typedef struct WindowAsyncAssetRequest {
    unsigned long request_id;
    unsigned long queued_msec;
    WindowAsyncAssetRequestKind kind;
    char* path;
    char* resource_name;
    WindowAssetLoadSuccessCallback success_callback;
    WindowAssetLoadErrorCallback error_callback;
    void* context;
    HWND notify_hwnd;
    unsigned long notify_message;
    struct WindowAsyncAssetRequest* next;
} WindowAsyncAssetRequest;

typedef struct WindowAsyncAssetCompletionNode {
    WindowAssetCompletion completion;
    HWND notify_hwnd;
    unsigned long notify_message;
    struct WindowAsyncAssetCompletionNode* next;
    struct WindowAsyncAssetCompletionNode* prev;
} WindowAsyncAssetCompletionNode;

static WindowClassRegistration* g_window_classes = 0;
static WindowHandleRegistration* g_window_handles = 0;
static WindowMenuRegistration* g_window_menus = 0;
static WindowQueuedMessage* g_window_queue_head = 0;
static WindowQueuedMessage* g_window_queue_tail = 0;
static WindowQueuedPacket* g_window_packet_queue_head = 0;
static WindowQueuedPacket* g_window_packet_queue_tail = 0;
static WindowAsyncAssetRequest* g_window_async_asset_request_head = 0;
static WindowAsyncAssetRequest* g_window_async_asset_request_tail = 0;
static WindowAsyncAssetCompletionNode* g_window_async_asset_completion_head = 0;
static WindowAsyncAssetCompletionNode* g_window_async_asset_completion_tail = 0;
static volatile unsigned long g_window_queue_lock_word = 0UL;
static unsigned long g_window_class_count = 0UL;
static unsigned long g_window_class_peak = 0UL;
static unsigned long g_window_handle_count = 0UL;
static unsigned long g_window_handle_peak = 0UL;
static unsigned long g_window_queue_count = 0UL;
static unsigned long g_window_queue_peak = 0UL;
static unsigned long g_window_packet_queue_count = 0UL;
static unsigned long g_window_packet_queue_peak = 0UL;
static unsigned long g_window_async_asset_next_request_id = 1UL;
static int g_window_async_asset_worker_started = 0;
static volatile int g_window_async_asset_shutdown_requested = 0;
static volatile int g_window_async_asset_shutdown_complete = 0;
static long g_window_gwes_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
static int g_window_quit_notified = 0;
static WindowMessageBoxState* g_window_active_message_box = 0;
static HMENU g_window_next_menu_handle = 1UL;

static void window_log_reply_wait(unsigned long expected_kind, unsigned long waited_msec);
static long window_message_box_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);
static void window_async_asset_worker_entry(unsigned long argument);
static int window_async_asset_request_timed_out(const WindowAsyncAssetRequest* request, unsigned long now_msec);
static long window_shutdown_async_asset_worker(void);
static void window_release_all_async_asset_completions(void);
static void window_drop_async_asset_completions_for_hwnd(HWND hwnd);

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
static void window_queue_lock(void);
static void window_queue_unlock(void);
static int window_queue_try_lock_once(void);
static long window_queue_enqueue(const MSG* message);
static WindowHandleRegistration* window_find_handle(HWND hwnd);

/*
 * Acquire the one process-local queue lock shared by the posted-message and
 * preserved-packet lists.
 *
 * Explorer now drives window.dll from both a UI thread and a controller thread.
 * Those threads can post local messages and preserve/recover synchronous GWES
 * replies concurrently, and the async asset loader also publishes background
 * requests from arbitrary callers. Every mutation of those shared FIFO or
 * intrusive next/prev links must therefore stay inside one small critical
 * section to avoid corrupting process-local state.
 *
 * @return Nothing.
 */
static void window_queue_lock(void) {
    while (!window_queue_try_lock_once()) {
    }
}

/*
 * Release the shared process-local queue lock.
 *
 * @return Nothing.
 */
static void window_queue_unlock(void) {
    unsigned long unlocked = 0UL;

    asm volatile("stlr %1, [%0]" : : "r"(&g_window_queue_lock_word), "r"(unlocked) : "memory");
}

/*
 * Attempt one acquire on the shared queue lock without helper-library calls.
 *
 * GCC lowered the legacy `__sync_lock_test_and_set` builtin to
 * `__aarch64_swp8_sync`, which leaked a synthetic `kernel` import into
 * `window.dll` and broke loader resolution for explorer. Keeping the acquire
 * path inlined with AArch64 exclusives preserves the required thread safety
 * without introducing any extra runtime dependency.
 *
 * @return Non-zero when the lock was acquired on this attempt.
 */
static int window_queue_try_lock_once(void) {
    unsigned long observed = 0UL;
    unsigned int store_failed = 0U;
    const unsigned long locked = 1UL;

    asm volatile(
        "ldaxr %0, [%2]\n"
        "cbnz %0, 1f\n"
        "stxr %w1, %3, [%2]\n"
        "b 2f\n"
        "1:\n"
        "mov %w1, #1\n"
        "2:\n"
        : "=&r"(observed), "=&r"(store_failed)
        : "r"(&g_window_queue_lock_word), "r"(locked)
        : "memory");

    return (observed == 0UL) && (store_failed == 0U);
}

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
 * Allocate one window-runtime heap block from the shared in-process allocator.
 *
 * window.dll now shares one process-local heap with ros_support and the rest of
 * the GUI stack, so queue and class registrations no longer bypass the demand-
 * paged allocator through raw SYS_MALLOC calls.
 *
 * @param size Byte count to allocate.
 * @return Heap block on success, or null on failure.
 */
static void* window_heap_alloc(unsigned long size) {
    return user_shared_heap_malloc((size_t)size);
}

/*
 * Release one window-runtime heap block acquired from `window_heap_alloc`.
 *
 * @param memory Heap block to free.
 * @return Nothing.
 */
static void window_heap_free(void* memory) {
    user_shared_heap_free(memory);
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
 * Measure one null-terminated ASCII string without relying on hosted libc.
 *
 * The async asset queue duplicates caller-owned path strings so background work
 * can outlive the enqueue call. Keeping this helper local avoids pulling in a
 * separate runtime dependency just to size those small copies.
 *
 * @param text Optional source string.
 * @return Character count before the terminating null.
 */
static unsigned long window_text_length(const char* text) {
    unsigned long length = 0UL;

    if (text == 0) {
        return 0UL;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

/*
 * Copy one byte range without depending on hosted libc helpers.
 *
 * Async file and DLL-section loads return independent heap buffers so callers
 * never borrow mapped image or scratch-stack storage. This small byte-copy
 * helper keeps that duplication self-contained inside `window.dll`.
 *
 * @param destination Writable destination buffer.
 * @param source Read-only source buffer.
 * @param size Byte count to copy.
 * @return Nothing.
 */
static void window_copy_memory(void* destination, const void* source, unsigned long size) {
    unsigned char* destination_bytes = (unsigned char*)destination;
    const unsigned char* source_bytes = (const unsigned char*)source;
    unsigned long index;

    if (destination == 0 || source == 0) {
        return;
    }

    for (index = 0UL; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

/*
 * Duplicate one caller-owned string into the shared GUI heap.
 *
 * Async work must retain its source path and optional section name after the
 * enqueue call returns, so the worker cannot keep borrowing the caller's stack
 * or mutable buffers.
 *
 * @param text Optional null-terminated source string.
 * @return Heap-backed copy on success, NULL on allocation failure or when `text` is NULL.
 */
static char* window_duplicate_text(const char* text) {
    char* copy;
    unsigned long size;

    if (text == 0) {
        return 0;
    }

    size = window_text_length(text) + 1UL;
    copy = (char*)window_heap_alloc(size);
    if (copy == 0) {
        return 0;
    }

    window_copy_text(copy, size, text);
    return copy;
}

/*
 * Return whether one DLL section name matches the requested ASCII label.
 *
 * The loader stores section names inline in an 8-byte fixed field, so embedded
 * resources are addressed by that short on-image name rather than by a Win32
 * `.rsrc` directory.
 *
 * @param section_name Fixed-width section name stored in the mapped image.
 * @param requested Null-terminated section name requested by the caller.
 * @return Non-zero when the names match exactly.
 */
static int window_section_name_matches(const char section_name[8], const char* requested) {
    unsigned long index;

    if (requested == 0 || requested[0] == '\0') {
        return 0;
    }

    for (index = 0UL; index < 8UL; ++index) {
        const char stored = section_name[index];
        const char expected = requested[index];

        if (expected == '\0') {
            if (stored != '\0') {
                return 0;
            }
            return 1;
        }
        if (stored != expected) {
            return 0;
        }
    }

    return requested[8] == '\0';
}

/*
 * Release one queued async asset request and its retained path strings.
 *
 * The worker frees request-owned metadata after invoking the callbacks so the
 * byte buffer lifetime is the only asset state that survives completion.
 *
 * @param request Request node to destroy.
 * @return Nothing.
 */
static void window_release_async_asset_request(WindowAsyncAssetRequest* request) {
    if (request == 0) {
        return;
    }

    if (request->path != 0) {
        window_heap_free(request->path);
    }
    if (request->resource_name != 0) {
        window_heap_free(request->resource_name);
    }
    window_heap_free(request);
}

/*
 * Release one queued async asset completion node.
 *
 * The completion queue temporarily owns any loaded byte buffer until the UI
 * thread claims it through `ReceiveAssetCompletion`. Teardown and dropped-owner
 * cleanup therefore need an explicit choice about whether the asset bytes
 * should be discarded alongside the queue node.
 *
 * @param completion Completion node to destroy.
 * @param release_bytes Non-zero when the staged byte buffer should also be freed.
 * @return Nothing.
 */
static void window_release_async_asset_completion_node(WindowAsyncAssetCompletionNode* completion, int release_bytes) {
    if (completion == 0) {
        return;
    }

    if (release_bytes && completion->completion.bytes != 0) {
        window_heap_free(completion->completion.bytes);
        completion->completion.bytes = 0;
    }
    window_heap_free(completion);
}

/*
 * Report whether one queued asset request exceeded the shared timeout budget.
 *
 * The current asset worker is single-threaded, so the timeout covers both time
 * spent waiting in the FIFO and time spent executing the read/decode-adjacent
 * callback work on that worker thread.
 *
 * @param request Queued or running request to evaluate.
 * @param now_msec Current monotonic uptime in milliseconds.
 * @return Non-zero when the request exceeded the timeout budget.
 */
static int window_async_asset_request_timed_out(const WindowAsyncAssetRequest* request, unsigned long now_msec) {
    if (request == 0) {
        return 0;
    }

    return (unsigned long)(now_msec - request->queued_msec) >= ROS_WINDOW_ASSET_TIMEOUT_MSEC;
}

/*
 * Release every queued async asset request without invoking callbacks.
 *
 * Process teardown can invalidate the callback code and owning UI state, so a
 * detach/reset path must discard any not-yet-started requests rather than
 * trying to complete them while the module is going away.
 *
 * @return Nothing.
 */
static void window_release_all_async_asset_requests(void) {
    WindowAsyncAssetRequest* request;

    window_queue_lock();
    request = g_window_async_asset_request_head;
    g_window_async_asset_request_head = 0;
    g_window_async_asset_request_tail = 0;
    window_queue_unlock();

    while (request != 0) {
        WindowAsyncAssetRequest* next = request->next;

        request->next = 0;
        window_release_async_asset_request(request);
        request = next;
    }
}

/*
 * Release every queued async asset completion that has not been claimed yet.
 *
 * If the owning process tears down before the UI thread drains its posted
 * completion messages, window.dll must reclaim both the queue nodes and any
 * staged asset bytes so shared-heap allocations do not leak across reloads.
 *
 * @return Nothing.
 */
static void window_release_all_async_asset_completions(void) {
    WindowAsyncAssetCompletionNode* completion;

    window_queue_lock();
    completion = g_window_async_asset_completion_head;
    g_window_async_asset_completion_head = 0;
    g_window_async_asset_completion_tail = 0;
    window_queue_unlock();

    while (completion != 0) {
        WindowAsyncAssetCompletionNode* next = completion->next;

        completion->next = 0;
        completion->prev = 0;
        window_release_async_asset_completion_node(completion, 1);
        completion = next;
    }
}

/*
 * Drop every queued async asset completion that targets one destroyed window.
 *
 * Posted completion messages are window-scoped. Once a local handle is gone,
 * any staged bytes waiting for that window are no longer reachable from user
 * code and must be reclaimed immediately.
 *
 * @param hwnd Destroyed local window handle.
 * @return Nothing.
 */
static void window_drop_async_asset_completions_for_hwnd(HWND hwnd) {
    WindowAsyncAssetCompletionNode* completion;

    if (hwnd == 0UL) {
        return;
    }

    window_queue_lock();
    completion = g_window_async_asset_completion_head;
    while (completion != 0) {
        WindowAsyncAssetCompletionNode* next = completion->next;

        if (completion->notify_hwnd == hwnd) {
            if (completion->prev != 0) {
                completion->prev->next = completion->next;
            }
            else {
                g_window_async_asset_completion_head = completion->next;
            }
            if (completion->next != 0) {
                completion->next->prev = completion->prev;
            }
            else {
                g_window_async_asset_completion_tail = completion->prev;
            }
            completion->next = 0;
            completion->prev = 0;
            window_queue_unlock();
            window_release_async_asset_completion_node(completion, 1);
            window_queue_lock();
        }

        completion = next;
    }
    window_queue_unlock();
}

/*
 * Remove the next pending async asset request from the worker FIFO.
 *
 * One dedicated background thread services the queue, so FIFO ordering keeps
 * request completion predictable for callers that stage a small asset bundle at
 * startup.
 *
 * @return Next request node, or NULL when the queue is empty.
 */
static WindowAsyncAssetRequest* window_take_async_asset_request(void) {
    WindowAsyncAssetRequest* request;

    window_queue_lock();
    request = g_window_async_asset_request_head;
    if (request != 0) {
        g_window_async_asset_request_head = request->next;
        if (g_window_async_asset_request_head == 0) {
            g_window_async_asset_request_tail = 0;
        }
        request->next = 0;
    }
    window_queue_unlock();
    return request;
}

/*
 * Queue one completed asset result for later pickup by the owner UI thread.
 *
 * The worker thread publishes a compact completion record, then wakes the UI
 * thread with one posted message. The owner later claims the record by request
 * id, which keeps all window-state mutation on the UI thread.
 *
 * @param request Completed request metadata.
 * @param status Zero on success, or a negative status code on failure.
 * @param bytes Loaded byte buffer on success, or NULL.
 * @param size Byte count stored in `bytes`.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_queue_async_asset_completion(
    const WindowAsyncAssetRequest* request,
    long status,
    void* bytes,
    unsigned long size) {
    WindowAsyncAssetCompletionNode* completion;
    MSG notification;
    long enqueue_status;

    if (request == 0 || request->notify_hwnd == 0UL || request->notify_message == 0UL) {
        if (bytes != 0) {
            window_heap_free(bytes);
        }
        return WINDOW_STATUS_ERROR;
    }
    if (window_find_handle(request->notify_hwnd) == 0) {
        if (bytes != 0) {
            window_heap_free(bytes);
        }
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    completion = (WindowAsyncAssetCompletionNode*)window_heap_alloc(sizeof(*completion));
    if (completion == 0) {
        if (bytes != 0) {
            window_heap_free(bytes);
        }
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    window_zero_memory(completion, sizeof(*completion));
    completion->completion.request_id = request->request_id;
    completion->completion.status = status;
    completion->completion.bytes = bytes;
    completion->completion.size = size;
    completion->completion.context = request->context;
    completion->notify_hwnd = request->notify_hwnd;
    completion->notify_message = request->notify_message;
    window_copy_text(completion->completion.path, sizeof(completion->completion.path), request->path != 0 ? request->path : "");
    window_copy_text(
        completion->completion.resource_name,
        sizeof(completion->completion.resource_name),
        request->resource_name != 0 ? request->resource_name : "");

    window_queue_lock();
    completion->prev = g_window_async_asset_completion_tail;
    if (g_window_async_asset_completion_tail != 0) {
        g_window_async_asset_completion_tail->next = completion;
    }
    else {
        g_window_async_asset_completion_head = completion;
    }
    g_window_async_asset_completion_tail = completion;
    window_queue_unlock();

    notification.hwnd = request->notify_hwnd;
    notification.message = request->notify_message;
    notification.wParam = request->request_id;
    notification.lParam = 0UL;
    enqueue_status = window_queue_enqueue(&notification);
    if (enqueue_status < 0L) {
        window_queue_lock();
        if (completion->prev != 0) {
            completion->prev->next = completion->next;
        }
        else {
            g_window_async_asset_completion_head = completion->next;
        }
        if (completion->next != 0) {
            completion->next->prev = completion->prev;
        }
        else {
            g_window_async_asset_completion_tail = completion->prev;
        }
        window_queue_unlock();
        completion->next = 0;
        completion->prev = 0;
        window_release_async_asset_completion_node(completion, 1);
        return enqueue_status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Ensure the dedicated async asset worker thread is running.
 *
 * One persistent worker amortizes startup cost and keeps repeated asset loads
 * off the UI thread without creating a new EL0 thread for every request.
 *
 * @return Zero on success, or a negative status code on thread-start failure.
 */
static long window_async_asset_ensure_worker_thread(void) {
    long status = ROS_USER_IPC_STATUS_OK;

    window_queue_lock();
    if (!g_window_async_asset_worker_started) {
        g_window_async_asset_shutdown_requested = 0;
        g_window_async_asset_shutdown_complete = 0;
        status = startUserThread((unsigned long)&window_async_asset_worker_entry, 0UL, "wnd.asset");
        if (status >= 0L) {
            g_window_async_asset_worker_started = 1;
            status = ROS_USER_IPC_STATUS_OK;
        }
    }
    window_queue_unlock();
    return status;
}

/*
 * Yield between background asset file-read chunks so UI work keeps moving.
 *
 * The cooperative scheduler does not automatically switch away from one EL0
 * thread that keeps issuing back-to-back syscalls. Without a short pause here,
 * the shared asset worker can make large image loads feel synchronous because
 * the owner UI thread does not get enough time to pump paint and input while
 * the worker drains the file.
 *
 * @param more_work_expected Non-zero when another file-read chunk is pending.
 * @return Nothing.
 */
static void window_asset_cooperative_file_yield(int more_work_expected) {
    if (!more_work_expected) {
        return;
    }

    (void)sleepMs(1UL);
}

/*
 * Read one whole file into a heap-backed byte buffer.
 *
 * Asset consumers such as font, icon, or image decoders typically want one
 * contiguous in-memory blob. Reading the file to completion here keeps that
 * higher-level code decoupled from filesystem chunking.
 *
 * @param path DOS-style file path to read.
 * @param bytes_out Receives the allocated byte buffer.
 * @param size_out Receives the final byte count.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_read_entire_file(const char* path, void** bytes_out, unsigned long* size_out) {
    UserPathInfo info;
    unsigned char* bytes = 0;
    unsigned long total = 0UL;
    unsigned long request = 0UL;
    long status;

    if (bytes_out == 0 || size_out == 0 || path == 0 || path[0] == '\0') {
        return WINDOW_STATUS_ERROR;
    }

    *bytes_out = 0;
    *size_out = 0UL;
    window_zero_memory(&info, sizeof(info));
    status = getPathInfo(path, &info);
    if (status < 0L) {
        return status;
    }

    if (info.size != 0UL) {
        bytes = (unsigned char*)window_heap_alloc(info.size);
        if (bytes == 0) {
            return ROS_USER_IPC_STATUS_NO_SPACE;
        }
    }

    while (total < info.size) {
        const unsigned long remaining = info.size - total;
        long read;

        request = remaining;
        if (request > WINDOW_ASSET_FILE_CHUNK) {
            request = WINDOW_ASSET_FILE_CHUNK;
        }

        read = readFile(path, total, (char*)(bytes + total), request);

        if (read < 0L) {
            window_heap_free(bytes);
            return read;
        }
        if (read == 0L) {
            window_heap_free(bytes);
            return WINDOW_STATUS_ERROR;
        }
        total += (unsigned long)read;
        window_asset_cooperative_file_yield(total < info.size);
    }

    *bytes_out = bytes;
    *size_out = info.size;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Copy one named DLL image section into an independent heap buffer.
 *
 * The async asset API intentionally returns copied bytes rather than pointers
 * into the mapped module image so callers can free the DLL immediately after
 * the request completes without dangling any consumer-facing memory.
 *
 * @param module Loaded DLL handle to inspect.
 * @param section_name Null-terminated section name to locate.
 * @param bytes_out Receives the copied section bytes.
 * @param size_out Receives the copied section length.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_copy_module_section(HMODULE module, const char* section_name, void** bytes_out, unsigned long* size_out) {
    const dll_header* header;
    const dll_section* sections;
    unsigned long section_index;

    if (bytes_out == 0 || size_out == 0 || module == 0UL || section_name == 0 || section_name[0] == '\0') {
        return WINDOW_STATUS_ERROR;
    }

    *bytes_out = 0;
    *size_out = 0UL;
    header = loaderImageHeader((unsigned long)module);
    if (header == 0) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    sections = (const dll_section*)((const unsigned char*)header + header->section_table_offset);
    for (section_index = 0UL; section_index < header->section_count; ++section_index) {
        const unsigned long section_size = (unsigned long)(sections[section_index].virtual_size != 0ULL
            ? sections[section_index].virtual_size
            : sections[section_index].raw_data_size);

        if (!window_section_name_matches(sections[section_index].name, section_name)) {
            continue;
        }
        if ((sections[section_index].flags & DLL_SEC_BSS) != 0U) {
            return WINDOW_STATUS_ERROR;
        }
        if (section_size != 0UL) {
            unsigned char* bytes = (unsigned char*)window_heap_alloc(section_size);

            if (bytes == 0) {
                return ROS_USER_IPC_STATUS_NO_SPACE;
            }
            window_copy_memory(bytes, (const unsigned char*)module + sections[section_index].virtual_address, section_size);
            *bytes_out = bytes;
        }
        *size_out = section_size;
        return ROS_USER_IPC_STATUS_OK;
    }

    return ROS_USER_IPC_STATUS_NOT_FOUND;
}

/*
 * Load one DLL on the background worker, copy a named section, and unload it again.
 *
 * The helper owns the temporary module lifetime so callers only see a flat byte
 * buffer and do not need to coordinate explicit `loadLibrary` / `freeLibrary`
 * calls around resource extraction.
 *
 * @param module_path DOS-style DLL path to open.
 * @param section_name Null-terminated image-section name.
 * @param bytes_out Receives the copied section bytes.
 * @param size_out Receives the copied section length.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_load_module_section_bytes(const char* module_path, const char* section_name, void** bytes_out, unsigned long* size_out) {
    HMODULE module;
    long status;

    if (module_path == 0 || module_path[0] == '\0' || section_name == 0 || section_name[0] == '\0') {
        return WINDOW_STATUS_ERROR;
    }

    module = loadLibrary(module_path);
    if (module == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = window_copy_module_section(module, section_name, bytes_out, size_out);
    (void)freeLibrary(module);
    return status;
}

/*
 * Execute one queued async asset request on the background worker.
 *
 * The public API supports raw files and DLL-embedded section assets, so the
 * worker routes both call shapes through one small switch that produces a
 * uniform `(bytes, size, status)` completion record.
 *
 * @param request Pending request to execute.
 * @param bytes_out Receives the loaded byte buffer.
 * @param size_out Receives the loaded byte count.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_execute_async_asset_request(const WindowAsyncAssetRequest* request, void** bytes_out, unsigned long* size_out) {
    if (request == 0) {
        return WINDOW_STATUS_ERROR;
    }

    switch (request->kind) {
    case WINDOW_ASSET_REQUEST_KIND_FILE:
        return window_read_entire_file(request->path, bytes_out, size_out);
    case WINDOW_ASSET_REQUEST_KIND_MODULE_SECTION:
        return window_load_module_section_bytes(request->path, request->resource_name, bytes_out, size_out);
    default:
        return WINDOW_STATUS_ERROR;
    }
}

/*
 * Drain queued asset requests and invoke their callbacks from one worker thread.
 *
 * The worker runs below normal priority so it yields to active UI threads while
 * still making forward progress on background font, icon, and image loads.
 * Keeping callback delivery on this same thread avoids creating a second parked
 * helper when the runtime still lacks a safe thread-exit path.
 *
 * @param argument Unused thread argument.
 * @return Nothing.
 */
static void window_async_asset_worker_entry(unsigned long argument) {
    (void)argument;
    // Keep asset delivery at normal priority because the current cooperative
    // scheduler can starve below-normal helper threads behind active shell or
    // UI work, which makes queued font/image staging look permanently hung.
    (void)setCurrentThreadPriority(USER_THREAD_PRIORITY_NORMAL);

    for (;;) {
        WindowAsyncAssetRequest* request = window_take_async_asset_request();

        if (request == 0) {
            if (g_window_async_asset_shutdown_requested) {
                g_window_async_asset_shutdown_complete = 1;
                g_window_async_asset_worker_started = 0;
                return;
            }
            (void)sleepMs(WINDOW_ASSET_WORKER_IDLE_SLEEP_MSEC);
            continue;
        }

        {
            void* bytes = 0;
            unsigned long size = 0UL;
            long status;

            if (window_async_asset_request_timed_out(request, getUptimeMs())) {
                status = ROS_WINDOW_ASSET_STATUS_TIMEOUT;
            }
            else {
                status = window_execute_async_asset_request(request, &bytes, &size);
                if (status >= 0L && window_async_asset_request_timed_out(request, getUptimeMs())) {
                    status = ROS_WINDOW_ASSET_STATUS_TIMEOUT;
                }
            }

            if (g_window_async_asset_shutdown_requested) {
                if (bytes != 0) {
                    window_heap_free(bytes);
                }
            }
            else if (request->notify_hwnd != 0UL && request->notify_message != 0UL) {
                status = window_queue_async_asset_completion(request, status, bytes, size);
                if (status < 0L && status != ROS_USER_IPC_STATUS_NOT_FOUND) {
                    writeLine("window.dll: asset completion queue failed");
                }
            }
            else if (status >= 0L) {
                if (request->success_callback != 0) {
                    request->success_callback(request->request_id, bytes, size, request->path, request->resource_name, request->context);
                }
                else if (bytes != 0) {
                    window_heap_free(bytes);
                }
            }
            else {
                if (bytes != 0) {
                    window_heap_free(bytes);
                }
                if (status == ROS_WINDOW_ASSET_STATUS_TIMEOUT) {
                    writeLine("window.dll: asset request timed out");
                }
                if (request->error_callback != 0) {
                    request->error_callback(request->request_id, status, request->path, request->resource_name, request->context);
                }
            }
        }

        window_release_async_asset_request(request);
    }
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
 * Link one menu registration into the local per-process registry.
 *
 * @param slot Newly initialized menu record.
 * @return Nothing.
 */
static void window_link_menu(WindowMenuRegistration* slot) {
    if (!slot) {
        return;
    }

    slot->next = g_window_menus;
    g_window_menus = slot;
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
 * Release every popup-menu description owned by the current process.
 *
 * @return Nothing.
 */
static void window_release_all_menus(void) {
    WindowMenuRegistration* current = g_window_menus;

    while (current != 0) {
        WindowMenuRegistration* next = current->next;

        window_heap_free(current);
        current = next;
    }

    g_window_menus = 0;
    g_window_next_menu_handle = 1UL;
}

/*
 * Release every queued local message owned by the current process.
 *
 * @return Nothing.
 */
static void window_release_all_messages(void) {
    WindowQueuedMessage* current;

    window_queue_lock();
    current = g_window_queue_head;
    g_window_queue_head = 0;
    g_window_queue_tail = 0;
    g_window_queue_count = 0UL;
    window_queue_unlock();

    while (current != 0) {
        WindowQueuedMessage* next = current->next;

        window_heap_free(current);
        current = next;
    }
}

/*
 * Release every preserved non-window IPC packet owned by the current process.
 *
 * @return Nothing.
 */
static void window_release_all_packets(void) {
    WindowQueuedPacket* current;

    window_queue_lock();
    current = g_window_packet_queue_head;
    g_window_packet_queue_head = 0;
    g_window_packet_queue_tail = 0;
    g_window_packet_queue_count = 0UL;
    window_queue_unlock();

    while (current != 0) {
        WindowQueuedPacket* next = current->next;

        window_heap_free(current);
        current = next;
    }
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
 * Find one local popup-menu description handle.
 *
 * @param menu Process-local menu handle.
 * @return Matching menu record, or NULL when the handle is unknown.
 */
static WindowMenuRegistration* window_find_menu(HMENU menu) {
    WindowMenuRegistration* current;

    for (current = g_window_menus; current != 0; current = current->next) {
        if (current->handle == menu) {
            return current;
        }
    }

    return 0;
}

/*
 * Remove one local popup-menu description from the per-process registry.
 *
 * @param menu Menu handle to unlink.
 * @return Non-zero when one menu record was removed.
 */
static int window_unlink_menu(HMENU menu) {
    WindowMenuRegistration** link;

    for (link = &g_window_menus; *link != 0; link = &((*link)->next)) {
        if ((*link)->handle != menu) {
            continue;
        }

        {
            WindowMenuRegistration* match = *link;

            *link = match->next;
            window_heap_free(match);
        }
        return 1;
    }

    return 0;
}

/*
 * Allocate one heap-backed popup-menu description record.
 *
 * @return Fresh record on success, or NULL when allocation fails.
 */
static WindowMenuRegistration* window_reserve_menu(void) {
    return (WindowMenuRegistration*)window_heap_alloc(sizeof(WindowMenuRegistration));
}

/*
 * Resolve one already-serialized menu handle inside the rooted menu tree map.
 *
 * @param menu_map Existing serialized menu-handle map.
 * @param menu_map_count Count of valid map entries.
 * @param handle Process-local menu handle to search.
 * @param menu_index Receives the serialized menu index.
 * @return Non-zero when the handle was already serialized.
 */
static int window_find_serialized_menu(const WindowEncodedMenuMap* menu_map, unsigned long menu_map_count, HMENU handle, unsigned long* menu_index) {
    unsigned long index;

    if (!menu_map || !menu_index) {
        return 0;
    }

    for (index = 0UL; index < menu_map_count; ++index) {
        if (menu_map[index].handle != handle) {
            continue;
        }

        *menu_index = menu_map[index].menu_index;
        return 1;
    }

    return 0;
}

/*
 * Serialize one rooted process-local menu subtree into the shared GWES wire model.
 *
 * @param handle Process-local menu handle that roots the subtree.
 * @param model Shared wire model being populated.
 * @param menu_map Serialized handle-to-index map used to avoid duplicate menus.
 * @param menu_map_count In-out count of populated map entries.
 * @param menu_index Receives the serialized index for `handle`.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_serialize_menu_tree(HMENU handle, RosWindowMenuModel* model, WindowEncodedMenuMap* menu_map, unsigned long* menu_map_count, unsigned long* menu_index) {
    WindowMenuRegistration* slot;
    unsigned long serialized_index;
    unsigned long index;

    if (handle == 0UL || !model || !menu_map || !menu_map_count || !menu_index) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (window_find_serialized_menu(menu_map, *menu_map_count, handle, &serialized_index)) {
        *menu_index = serialized_index;
        return ROS_USER_IPC_STATUS_OK;
    }

    slot = window_find_menu(handle);
    if (!slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (model->menu_count >= ROS_WINDOW_MENU_MAX_MENUS || *menu_map_count >= ROS_WINDOW_MENU_MAX_MENUS) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    serialized_index = model->menu_count++;
    menu_map[*menu_map_count].handle = handle;
    menu_map[*menu_map_count].menu_index = serialized_index;
    ++(*menu_map_count);

    model->menus[serialized_index].first_item = model->item_count;
    model->menus[serialized_index].item_count = slot->item_count;
    for (index = 0UL; index < slot->item_count; ++index) {
        RosWindowMenuItem* encoded_item;
        const WindowLocalMenuItem* local_item = &slot->items[index];

        if (model->item_count >= ROS_WINDOW_MENU_MAX_ITEMS) {
            return ROS_USER_IPC_STATUS_NO_SPACE;
        }

        encoded_item = &model->items[model->item_count++];
        window_zero_memory(encoded_item, sizeof(*encoded_item));
        encoded_item->command_id = local_item->command_id;
        encoded_item->flags = local_item->flags;
        encoded_item->hotkey = local_item->hotkey;
        encoded_item->submenu_index = ROS_WINDOW_MENU_INVALID_INDEX;
        window_copy_text(encoded_item->text, sizeof(encoded_item->text), local_item->text);
        if ((local_item->flags & ROS_MENU_ITEM_FLAG_SUBMENU) != 0UL) {
            long status = window_serialize_menu_tree(local_item->submenu_handle, model, menu_map, menu_map_count, &encoded_item->submenu_index);

            if (status < 0L) {
                return status;
            }
        }
    }

    *menu_index = serialized_index;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Report whether one local handle belongs to the supplied owner subtree.
 *
 * Modal message boxes only need to suppress input for the nominated owner and
 * its child windows in the current process, so a small parent-chain walk is
 * sufficient and keeps the policy entirely inside window.dll.
 *
 * @param hwnd Candidate target handle.
 * @param owner_hwnd Owner root handle to test against.
 * @return Non-zero when the candidate belongs to the owner's subtree.
 */
static int window_handle_is_descendant_of(HWND hwnd, HWND owner_hwnd) {
    WindowHandleRegistration* current;

    if (hwnd == 0UL || owner_hwnd == 0UL) {
        return 0;
    }

    current = window_find_handle(hwnd);
    while (current != 0) {
        if (current->hwnd == owner_hwnd) {
            return 1;
        }
        if (current->parent == 0UL) {
            break;
        }
        current = window_find_handle(current->parent);
    }

    return 0;
}

/*
 * Report whether one message should be suppressed for the modal owner subtree.
 *
 * The first modal policy blocks interactive input and close attempts against
 * the owner while still allowing repaint and layout traffic so the covered
 * window tree stays visually current behind the dialog.
 *
 * @param message Window message identifier.
 * @return Non-zero when the modal loop should swallow the message.
 */
static int window_message_is_modal_blocked(unsigned long message) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSECLICKED:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CLOSE:
    case WM_TIMER:
        return 1;
    default:
        return 0;
    }
}

/*
 * Remove one local window-handle registration from the per-process registry.
 *
 * Explicit window destruction must drop the local dispatch record immediately
 * so later `SendMessage` / `PostMessage` calls cannot target a stale handle.
 *
 * @param hwnd Window handle to unlink.
 * @return Non-zero when one handle record was removed.
 */
static int window_unlink_handle(HWND hwnd) {
    WindowHandleRegistration** link;

    for (link = &g_window_handles; *link != 0; link = &((*link)->next)) {
        if ((*link)->hwnd != hwnd) {
            continue;
        }

        {
            WindowHandleRegistration* match = *link;

            *link = match->next;
            window_heap_free(match);
            if (g_window_handle_count != 0UL) {
                --g_window_handle_count;
            }
        }
        return 1;
    }

    return 0;
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
    window_queue_lock();
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
    window_queue_unlock();
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
static int window_queue_coalesce_existing_locked(const MSG* message) {
    WindowQueuedMessage* queued;

    if (!message || !window_message_should_coalesce(message->message) || g_window_queue_count == 0UL) {
        return 0;
    }

    for (queued = g_window_queue_tail; queued != 0; queued = queued->prev) {
        if (queued->message.hwnd != message->hwnd || queued->message.message != message->message) {
            continue;
        }
        if (message->message == WM_TIMER && queued->message.wParam != message->wParam) {
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
    long status;

    if (!message) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_queue_lock();
    if (window_queue_coalesce_existing_locked(message)) {
        window_queue_unlock();
        return ROS_USER_IPC_STATUS_OK;
    }
    window_queue_unlock();

    status = window_queue_push_raw(message);
    return status;
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
    window_release_all_menus();
    window_release_all_messages();
    window_release_all_packets();
    window_release_all_async_asset_requests();
    window_release_all_async_asset_completions();
    g_window_class_peak = 0UL;
    g_window_handle_peak = 0UL;
    g_window_queue_peak = 0UL;
    g_window_packet_queue_peak = 0UL;
    g_window_async_asset_next_request_id = 1UL;
    g_window_async_asset_worker_started = 0;
    g_window_async_asset_shutdown_requested = 0;
    g_window_async_asset_shutdown_complete = 0;
    g_window_gwes_pid = ROS_USER_IPC_STATUS_NOT_FOUND;
    g_window_quit_notified = 0;
}

/*
 * Stop the process-local async asset worker during DLL detach.
 *
 * `window.dll` may be detached before the surrounding process object is fully
 * destroyed. Clearing queued requests, suppressing callbacks, and then waiting
 * for the worker to leave its loop keeps the helper thread from executing code
 * out of an unmapped module.
 *
 * @return Zero on success, or a negative status code on timeout.
 */
static long window_shutdown_async_asset_worker(void) {
    unsigned long deadline_msec;

    if (!g_window_async_asset_worker_started) {
        g_window_async_asset_shutdown_complete = 1;
        return ROS_USER_IPC_STATUS_OK;
    }

    window_release_all_async_asset_requests();
    g_window_async_asset_shutdown_requested = 1;
    deadline_msec = getUptimeMs() + WINDOW_ASSET_SHUTDOWN_WAIT_MSEC;
    while (!g_window_async_asset_shutdown_complete) {
        if ((long)(deadline_msec - getUptimeMs()) <= 0L) {
            return ROS_USER_IPC_STATUS_BUSY;
        }
        (void)sleepMs(1UL);
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Queue one background asset request after validating and duplicating its metadata.
 *
 * Both public async asset entrypoints share the same request-building path so
 * the worker FIFO, request-id allocation, and thread-start policy stay fully
 * consistent no matter where the bytes come from.
 *
 * @param kind File-backed or DLL-section-backed request kind.
 * @param path File path or DLL path that sources the asset.
 * @param resource_name NULL for files, or the DLL section name for embedded assets.
 * @param success_callback Completion callback for successful loads.
 * @param error_callback Completion callback for failed loads.
 * @param context Opaque caller-owned cookie forwarded to callbacks.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
static long window_queue_async_asset_request(
    WindowAsyncAssetRequestKind kind,
    const char* path,
    const char* resource_name,
    WindowAssetLoadSuccessCallback success_callback,
    WindowAssetLoadErrorCallback error_callback,
    void* context,
    HWND notify_hwnd,
    unsigned long notify_message) {
    WindowAsyncAssetRequest* request;
    long status;

    if (path == 0 || path[0] == '\0') {
        return WINDOW_STATUS_ERROR;
    }
    if (success_callback == 0 && (notify_hwnd == 0UL || notify_message == 0UL)) {
        return WINDOW_STATUS_ERROR;
    }
    if (g_window_async_asset_shutdown_requested) {
        return ROS_USER_IPC_STATUS_BUSY;
    }
    if (kind == WINDOW_ASSET_REQUEST_KIND_MODULE_SECTION
        && (resource_name == 0 || resource_name[0] == '\0' || window_text_length(resource_name) >= ROS_WINDOW_ASSET_SECTION_NAME_MAX)) {
        return WINDOW_STATUS_ERROR;
    }

    request = (WindowAsyncAssetRequest*)window_heap_alloc(sizeof(*request));
    if (request == 0) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    window_zero_memory(request, sizeof(*request));
    request->queued_msec = getUptimeMs();
    request->kind = kind;
    request->path = window_duplicate_text(path);
    if (request->path == 0) {
        window_heap_free(request);
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }
    if (resource_name != 0) {
        request->resource_name = window_duplicate_text(resource_name);
        if (request->resource_name == 0) {
            window_release_async_asset_request(request);
            return ROS_USER_IPC_STATUS_NO_SPACE;
        }
    }
    request->success_callback = success_callback;
    request->error_callback = error_callback;
    request->context = context;
    request->notify_hwnd = notify_hwnd;
    request->notify_message = notify_message;

    status = window_async_asset_ensure_worker_thread();
    if (status < 0L) {
        window_release_async_asset_request(request);
        return status;
    }

    window_queue_lock();
    request->request_id = g_window_async_asset_next_request_id++;
    if (g_window_async_asset_next_request_id == 0UL) {
        g_window_async_asset_next_request_id = 1UL;
    }
    if (g_window_async_asset_request_tail != 0) {
        g_window_async_asset_request_tail->next = request;
    }
    else {
        g_window_async_asset_request_head = request;
    }
    g_window_async_asset_request_tail = request;
    window_queue_unlock();
    return (long)request->request_id;
}

/*
 * Pop one message from the local per-process queue.
 *
 * @param message Receives the next queued message.
 * @return One when a message was copied, or zero when the queue is empty.
 */
static int window_queue_pop(MSG* message) {
    WindowQueuedMessage* queued;

    if (!message) {
        return 0;
    }

    window_queue_lock();
    if (g_window_queue_count == 0UL) {
        window_queue_unlock();
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
    window_queue_unlock();
    window_heap_free(queued);
    return 1;
}

/*
 * Drop any posted messages that still target one destroyed local window.
 *
 * UI teardown can race previously posted repaint or completion messages. Once a
 * local handle is gone those queued entries are no longer actionable, so remove
 * them proactively instead of letting dispatch log a missing-handle warning.
 *
 * @param hwnd Destroyed local window handle.
 * @return Nothing.
 */
static void window_queue_drop_handle(HWND hwnd) {
    WindowQueuedMessage* queued;

    if (hwnd == 0UL) {
        return;
    }

    window_queue_lock();
    queued = g_window_queue_head;
    while (queued != 0) {
        WindowQueuedMessage* next = queued->next;

        if (queued->message.hwnd == hwnd) {
            if (queued->prev != 0) {
                queued->prev->next = queued->next;
            }
            else {
                g_window_queue_head = queued->next;
            }
            if (queued->next != 0) {
                queued->next->prev = queued->prev;
            }
            else {
                g_window_queue_tail = queued->prev;
            }
            if (g_window_queue_count != 0UL) {
                --g_window_queue_count;
            }
            window_heap_free(queued);
        }

        queued = next;
    }
    window_queue_unlock();
    window_drop_async_asset_completions_for_hwnd(hwnd);
}

/*
 * Push one preserved non-window IPC packet into the process-local queue.
 *
 * @param packet Packet to preserve for non-UI threads.
 * @return Zero on success, or `ROS_USER_IPC_STATUS_NO_SPACE` on allocation failure.
 */
static long window_packet_queue_push(const UserIpcMessage* packet) {
    WindowQueuedPacket* node;

    if (!packet) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    node = (WindowQueuedPacket*)window_heap_alloc(sizeof(WindowQueuedPacket));
    if (!node) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    window_zero_memory(node, sizeof(*node));
    node->packet = *packet;
    window_queue_lock();
    node->prev = g_window_packet_queue_tail;
    if (g_window_packet_queue_tail != 0) {
        g_window_packet_queue_tail->next = node;
    }
    else {
        g_window_packet_queue_head = node;
    }

    g_window_packet_queue_tail = node;
    ++g_window_packet_queue_count;
    if (g_window_packet_queue_count > g_window_packet_queue_peak) {
        g_window_packet_queue_peak = g_window_packet_queue_count;
    }
    window_queue_unlock();
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Pop one preserved non-window IPC packet from the process-local queue.
 *
 * @param packet Receives the next preserved packet.
 * @return One when a packet was copied, or zero when the queue is empty.
 */
static int window_packet_queue_pop(UserIpcMessage* packet) {
    WindowQueuedPacket* queued;

    if (!packet) {
        return 0;
    }

    window_queue_lock();
    if (g_window_packet_queue_count == 0UL) {
        window_queue_unlock();
        return 0;
    }

    queued = g_window_packet_queue_head;
    *packet = queued->packet;
    g_window_packet_queue_head = queued->next;
    if (g_window_packet_queue_head != 0) {
        g_window_packet_queue_head->prev = 0;
    }
    else {
        g_window_packet_queue_tail = 0;
    }

    --g_window_packet_queue_count;
    window_queue_unlock();
    window_heap_free(queued);
    return 1;
}

/*
 * Remove one preserved GWES reply packet with the requested reply kind.
 *
 * Explorer now has one controller thread and one UI thread sharing the same
 * process IPC stream. The controller can legally drain a synchronous GWES
 * reply before the UI thread's `window_receive_reply()` call sees it, so the
 * reply must remain recoverable from the preserved packet queue instead of
 * forcing the waiting thread to time out forever.
 *
 * @param expected_kind Reply kind the caller is waiting for.
 * @param packet Receives the preserved reply packet.
 * @return One when a matching reply was removed, or zero when none is queued.
 */
static int window_packet_queue_take_reply(unsigned long expected_kind, UserIpcMessage* packet) {
    WindowQueuedPacket* queued;
    WindowQueuedPacket* match = 0;

    if (!packet) {
        return 0;
    }

    window_queue_lock();
    for (queued = g_window_packet_queue_head; queued != 0; queued = queued->next) {
        if (queued->packet.protocol != ROS_WINDOW_SERVER_PROTOCOL || queued->packet.kind != expected_kind) {
            continue;
        }

        match = queued;
        *packet = queued->packet;
        if (queued->prev != 0) {
            queued->prev->next = queued->next;
        }
        else {
            g_window_packet_queue_head = queued->next;
        }
        if (queued->next != 0) {
            queued->next->prev = queued->prev;
        }
        else {
            g_window_packet_queue_tail = queued->prev;
        }

        --g_window_packet_queue_count;
        break;
    }

    window_queue_unlock();

    if (match != 0) {
        window_heap_free(match);
        return 1;
    }

    return 0;
}

/*
 * Remove one preserved non-window packet from the shared process queue.
 *
 * `ReceiveProcessIpcMessage()` is the escape hatch for shell/controller helper
 * threads that need application-defined IPC. GWES reply packets for
 * `CreateWindowEx`, `MoveWindow`, and similar synchronous helpers must stay in
 * the preserved queue so the UI thread's `window_receive_reply()` path can
 * recover them even when another thread is polling concurrently.
 *
 * @param packet Receives the next preserved non-window packet.
 * @return One when a non-window packet was removed, or zero when none is queued.
 */
static int window_packet_queue_take_non_window(UserIpcMessage* packet) {
    WindowQueuedPacket* queued;
    WindowQueuedPacket* match = 0;

    if (!packet) {
        return 0;
    }

    window_queue_lock();
    for (queued = g_window_packet_queue_head; queued != 0; queued = queued->next) {
        if (queued->packet.protocol == ROS_WINDOW_SERVER_PROTOCOL) {
            continue;
        }

        match = queued;
        *packet = queued->packet;
        if (queued->prev != 0) {
            queued->prev->next = queued->next;
        }
        else {
            g_window_packet_queue_head = queued->next;
        }
        if (queued->next != 0) {
            queued->next->prev = queued->prev;
        }
        else {
            g_window_packet_queue_tail = queued->prev;
        }

        --g_window_packet_queue_count;
        break;
    }

    window_queue_unlock();

    if (match != 0) {
        window_heap_free(match);
        return 1;
    }

    return 0;
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

    if (window_packet_to_message(packet, &queued_message)) {
        return window_queue_enqueue(&queued_message);
    }

    return WINDOW_QUEUE_IGNORED;
}

/*
 * Route one inbound process packet to either the UI queue or the preserved
 * non-window IPC queue.
 *
 * @param packet Packet received from the shared process IPC queue.
 * @return Zero on success, or a negative status code on queue failure.
 */
static long window_route_process_packet(const UserIpcMessage* packet) {
    long status;

    status = window_enqueue_server_packet(packet);
    if (status == WINDOW_QUEUE_IGNORED) {
        return window_packet_queue_push(packet);
    }

    return status;
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

        status = window_route_process_packet(&packet);
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
        if (window_packet_queue_take_reply(expected_kind, packet)) {
            return ROS_USER_IPC_STATUS_OK;
        }

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
        if (status < 0L) {
            return status;
        }
        if (packet->protocol == ROS_WINDOW_SERVER_PROTOCOL && packet->kind == expected_kind) {
            return ROS_USER_IPC_STATUS_OK;
        }

        status = window_route_process_packet(packet);
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

/*
 * Ask GWES to destroy one live window owned by the current process.
 *
 * @param hwnd Target local window handle.
 * @return Zero on success, or a negative status code on failure.
 */
static long window_send_destroy_window(HWND hwnd) {
    UserIpcMessage request;

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_DESTROY_WINDOW;
    request.arg0 = (unsigned long)hwnd;
    return window_send_request(&request);
}

/*
 * Create one new process-local popup-menu description.
 *
 * @return Non-zero menu handle on success, or zero on allocation failure.
 */
HMENU CreateMenu(void) {
    WindowMenuRegistration* slot = window_reserve_menu();

    if (!slot) {
        return 0UL;
    }

    window_zero_memory(slot, sizeof(*slot));
    slot->handle = g_window_next_menu_handle++;
    if (slot->handle == 0UL) {
        slot->handle = g_window_next_menu_handle++;
    }
    window_link_menu(slot);
    return slot->handle;
}

/*
 * Destroy one process-local popup-menu description.
 *
 * @param menu Menu handle previously returned by `CreateMenu`.
 * @return Zero on success, or a negative status code on failure.
 */
long DestroyMenu(HMENU menu) {
    if (menu == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    return window_unlink_menu(menu) ? ROS_USER_IPC_STATUS_OK : ROS_USER_IPC_STATUS_NOT_FOUND;
}

/*
 * Append one command item to a process-local popup-menu description.
 *
 * @param menu Target menu handle.
 * @param command_id Command identifier returned on selection.
 * @param flags Item-state flags.
 * @param text Visible menu caption.
 * @param hotkey Optional ASCII mnemonic.
 * @return Zero on success, or a negative status code on failure.
 */
long AppendMenuItem(HMENU menu, unsigned long command_id, unsigned long flags, const char* text, unsigned long hotkey) {
    WindowMenuRegistration* slot;
    WindowLocalMenuItem* item;

    if (menu == 0UL || command_id == 0UL || text == 0 || text[0] == '\0') {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    slot = window_find_menu(menu);
    if (!slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (slot->item_count >= ROS_WINDOW_MENU_MAX_ITEMS) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    item = &slot->items[slot->item_count++];
    window_zero_memory(item, sizeof(*item));
    item->command_id = command_id;
    item->flags = flags & ~(ROS_MENU_ITEM_FLAG_SEPARATOR | ROS_MENU_ITEM_FLAG_SUBMENU);
    item->hotkey = hotkey;
    item->submenu_handle = 0UL;
    window_copy_text(item->text, sizeof(item->text), text);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Append one submenu row to a process-local popup-menu description.
 *
 * @param menu Parent menu handle.
 * @param submenu Child menu handle that should open from this row.
 * @param flags Item-state flags.
 * @param text Visible menu caption.
 * @param hotkey Optional ASCII mnemonic.
 * @return Zero on success, or a negative status code on failure.
 */
long AppendSubMenu(HMENU menu, HMENU submenu, unsigned long flags, const char* text, unsigned long hotkey) {
    WindowMenuRegistration* slot;
    WindowLocalMenuItem* item;

    if (menu == 0UL || submenu == 0UL || menu == submenu || text == 0 || text[0] == '\0') {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    slot = window_find_menu(menu);
    if (!slot || !window_find_menu(submenu)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (slot->item_count >= ROS_WINDOW_MENU_MAX_ITEMS) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    item = &slot->items[slot->item_count++];
    window_zero_memory(item, sizeof(*item));
    item->flags = (flags & ~ROS_MENU_ITEM_FLAG_SEPARATOR) | ROS_MENU_ITEM_FLAG_SUBMENU;
    item->hotkey = hotkey;
    item->submenu_handle = submenu;
    window_copy_text(item->text, sizeof(item->text), text);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Append one separator row to a process-local popup-menu description.
 *
 * @param menu Target menu handle.
 * @return Zero on success, or a negative status code on failure.
 */
long AppendMenuSeparator(HMENU menu) {
    WindowMenuRegistration* slot;
    WindowLocalMenuItem* item;

    if (menu == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    slot = window_find_menu(menu);
    if (!slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (slot->item_count >= ROS_WINDOW_MENU_MAX_ITEMS) {
        return ROS_USER_IPC_STATUS_NO_SPACE;
    }

    item = &slot->items[slot->item_count++];
    window_zero_memory(item, sizeof(*item));
    item->flags = ROS_MENU_ITEM_FLAG_SEPARATOR;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Ask GWES to show and track one authoritative popup-menu session.
 *
 * @param menu Process-local menu handle to marshal into GWES.
 * @param flags Popup tracking flags.
 * @param x Desktop X coordinate for the popup origin.
 * @param y Desktop Y coordinate for the popup origin.
 * @param owner Local owner window that receives `WM_COMMAND` when requested.
 * @return Command identifier, one, zero, or a negative status code.
 */
long TrackPopupMenu(HMENU menu, unsigned long flags, long x, long y, HWND owner) {
    WindowMenuRegistration* slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    RosWindowMenuModel* shared_model = 0;
    WindowEncodedMenuMap menu_map[ROS_WINDOW_MENU_MAX_MENUS];
    unsigned long menu_map_count = 0UL;
    unsigned long root_menu_index = ROS_WINDOW_MENU_INVALID_INDEX;
    char shared_name[ROS_WINDOW_MENU_SHARED_NAME_MAX];
    long status;

    if (menu == 0UL || owner == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    slot = window_find_menu(menu);
    if (!slot || slot->item_count == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (!window_find_handle(owner)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    WindowBuildMenuSharedName(shared_name, sizeof(shared_name), owner, menu);
    status = acquireSharedMemoryRegion(shared_name, sizeof(*shared_model), (void**)&shared_model);
    if (status < 0L || shared_model == 0) {
        return status < 0L ? status : ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(shared_model, sizeof(*shared_model));
    shared_model->version = ROS_WINDOW_MENU_MODEL_VERSION;
    shared_model->root_menu_index = ROS_WINDOW_MENU_INVALID_INDEX;
    shared_model->menu_count = 0UL;
    shared_model->item_count = 0UL;
    window_zero_memory(menu_map, sizeof(menu_map));
    status = window_serialize_menu_tree(menu, shared_model, menu_map, &menu_map_count, &root_menu_index);
    if (status < 0L) {
        return status;
    }
    shared_model->root_menu_index = root_menu_index;

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU;
    request.arg0 = (unsigned long)owner;
    request.arg1 = WindowPackSignedPair(x, y);
    request.arg2 = flags;
    window_copy_text(request.text, sizeof(request.text), shared_name);

    status = window_send_request(&request);
    if (status < 0L) {
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY, &reply);
    if (status < 0L) {
        return status;
    }
    if ((long)reply.arg0 < 0L) {
        return (long)reply.arg0;
    }
    if (reply.arg1 == 0UL) {
        return 0L;
    }
    if ((flags & ROS_MENU_TRACK_RETURNCMD) != 0UL) {
        return (long)reply.arg1;
    }

    status = PostMessage(owner, WM_COMMAND, reply.arg1, 0UL);
    if (status < 0L) {
        return status;
    }

    return 1L;
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
 * Destroy one live window through GWES and drop the local dispatch record.
 *
 * @param hwnd Target local window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long DestroyWindow(HWND hwnd) {
    WindowHandleRegistration* handle_slot;
    UserIpcMessage reply;
    long status;

    if (hwnd == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    handle_slot = window_find_handle(hwnd);
    if (!handle_slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = window_send_destroy_window(hwnd);
    if (status < 0L) {
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_DESTROY_WINDOW_REPLY, &reply);
    if (status < 0L) {
        return status;
    }
    if ((long)reply.arg0 < 0L) {
        return (long)reply.arg0;
    }

    (void)window_unlink_handle(hwnd);
    window_queue_drop_handle(hwnd);
    return ROS_USER_IPC_STATUS_OK;
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
    handle_slot->parent = params->parent;
    handle_slot->proc = class_slot->proc;
    handle_slot->x = params->x;
    handle_slot->y = params->y;
    handle_slot->width = params->width;
    handle_slot->height = params->height;
    handle_slot->style = params->style;
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

/*
 * Queue the local bounds-change notifications that client code expects after one
 * successful server-side move or resize completes.
 *
 * GWES owns the authoritative geometry and shared-surface reallocation, but the
 * client-side window procedure still needs a local `WM_MOVE` / `WM_SIZE` pair so
 * it can reacquire the resized surface mapping and schedule a repaint against the
 * new dimensions. Without this handoff, windows that are created at a placeholder
 * size and resized immediately afterward keep painting into the stale mapping.
 *
 * @param hwnd Target window handle.
 * @param x New window X coordinate.
 * @param y New window Y coordinate.
 * @param width New window width.
 * @param height New window height.
 * @return Nothing.
 */
static void window_post_local_bounds_change(HWND hwnd, long x, long y, unsigned long width, unsigned long height) {
    if (hwnd == 0UL) {
        return;
    }

    (void)PostMessage(hwnd, WM_MOVE, 0UL, WindowPackSignedPair(x, y));
    (void)PostMessage(hwnd, WM_SIZE, 0UL, window_pack_pair(width, height));
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

    if ((long)reply.arg0 >= 0L) {
        handle_slot->x = x;
        handle_slot->y = y;
        handle_slot->width = width;
        handle_slot->height = height;
        window_post_local_bounds_change(hwnd, x, y, width, height);
    }

    return (long)reply.arg0;
}

/*
 * Request that GWES raise one root window and make it the active foreground target.
 *
 * The shell taskbar needs to activate foreign application windows, so this path
 * deliberately skips local-handle ownership checks and lets GWES validate the
 * incoming handle against its retained window table.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long SetForegroundWindow(HWND hwnd) {
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (hwnd == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW;
    request.arg0 = (unsigned long)hwnd;

    status = window_send_request(&request);
    if (status < 0L) {
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW_REPLY, &reply);
    if (status < 0L) {
        return status;
    }

    return (long)reply.arg0;
}

/*
 * Queue one foreground activation request without waiting for a reply.
 *
 * Taskbar and start-menu interactions only need to trigger the transition, so
 * waiting for the round trip back from GWES would just add avoidable latency to
 * shell input handling under load.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long PostSetForegroundWindow(HWND hwnd) {
    UserIpcMessage request;

    if (hwnd == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_POST_FOREGROUND_WINDOW;
    request.arg0 = (unsigned long)hwnd;
    return window_send_request(&request);
}

/*
 * Register or refresh one per-window timer through GWES.
 *
 * @param hwnd Target local window handle.
 * @param timer_id Existing timer identifier, or zero to allocate one.
 * @param interval_msec Timer period in milliseconds.
 * @return Stable timer identifier on success, or zero on failure.
 */
unsigned long SetTimer(HWND hwnd, unsigned long timer_id, unsigned long interval_msec) {
    WindowHandleRegistration* handle_slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (hwnd == 0UL || interval_msec == 0UL) {
        return 0UL;
    }

    handle_slot = window_find_handle(hwnd);
    if (!handle_slot) {
        return 0UL;
    }

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_SET_TIMER;
    request.arg0 = (unsigned long)hwnd;
    request.arg1 = timer_id;
    request.arg2 = interval_msec;

    status = window_send_request(&request);
    if (status < 0L) {
        return 0UL;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_SET_TIMER_REPLY, &reply);
    if (status < 0L) {
        return 0UL;
    }
    if ((long)reply.arg0 < 0L) {
        return 0UL;
    }

    return reply.arg1;
}

/*
 * Cancel one previously registered per-window timer through GWES.
 *
 * @param hwnd Target local window handle.
 * @param timer_id Timer identifier previously returned by `SetTimer`.
 * @return Zero on success, or a negative status code on failure.
 */
long KillTimer(HWND hwnd, unsigned long timer_id) {
    WindowHandleRegistration* handle_slot;
    UserIpcMessage request;
    UserIpcMessage reply;
    long status;

    if (hwnd == 0UL || timer_id == 0UL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    handle_slot = window_find_handle(hwnd);
    if (!handle_slot) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(&request, sizeof(request));
    request.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    request.kind = ROS_WINDOW_SERVER_KIND_KILL_TIMER;
    request.arg0 = (unsigned long)hwnd;
    request.arg1 = timer_id;

    status = window_send_request(&request);
    if (status < 0L) {
        return status;
    }

    status = window_receive_reply(ROS_WINDOW_SERVER_KIND_KILL_TIMER_REPLY, &reply);
    if (status < 0L) {
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
    if (!handle_slot) {
        if (!g_window_quit_notified) {
            writeLine("window.dll: send missing handle");
        }
        window_drop_async_asset_completions_for_hwnd(hwnd);
        return 0L;
    }
    if (!handle_slot->proc) {
        writeLine("window.dll: send missing proc");
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

long ReceiveProcessIpcMessage(UserIpcMessage* packet, unsigned long flags) {
    UserIpcMessage incoming;
    const unsigned long timeout_msec = ROS_USER_IPC_RECEIVE_TIMEOUT_MSEC(flags);
    const unsigned long wait_chunk_msec = timeout_msec != 0UL && timeout_msec < WINDOW_REPLY_WAIT_MSEC
        ? timeout_msec
        : WINDOW_REPLY_WAIT_MSEC;
    unsigned long waited_msec = 0UL;
    long status;

    if (!packet) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (window_packet_queue_take_non_window(packet)) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if ((flags & ROS_USER_IPC_RECEIVE_WAIT) == 0UL) {
        return ROS_USER_IPC_STATUS_BUSY;
    }

    for (;;) {
        if (window_packet_queue_take_non_window(packet)) {
            return ROS_USER_IPC_STATUS_OK;
        }

        status = receiveUserIpcMessage(
            &incoming,
            ROS_USER_IPC_RECEIVE_WAIT | ROS_USER_IPC_RECEIVE_TIMEOUT_ENCODE(wait_chunk_msec));
        if (status == ROS_USER_IPC_STATUS_BUSY) {
            waited_msec += wait_chunk_msec;
            if (timeout_msec != 0UL && waited_msec >= timeout_msec) {
                return ROS_USER_IPC_STATUS_BUSY;
            }
            continue;
        }
        if (status < 0L) {
            return status;
        }

        status = window_route_process_packet(&incoming);
        if (status < 0L) {
            return status;
        }
    }
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

        status = window_route_process_packet(&packet);
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

/*
 * Measure one message-box body block and clamp it to the fixed first-version layout.
 *
 * @param text Optional message text.
 * @param height_out Receives the suggested body height in pixels.
 * @return Nothing.
 */
static void window_measure_message_box_text(const char* text, unsigned long* height_out) {
    RosGdiFont font;
    unsigned long text_width = 0UL;
    unsigned long text_height = 0UL;

    if (height_out == 0) {
        return;
    }

    *height_out = WINDOW_MESSAGEBOX_DEFAULT_HEIGHT;
    window_zero_memory(&font, sizeof(font));
    if (GdiLoadFont(0, 14UL, &font) < 0L) {
        return;
    }

    if (GdiMeasureText(&font, text != 0 ? text : "", &text_width, &text_height) >= 0L && text_height != 0UL) {
        unsigned long desired = text_height + 76UL;

        if (desired > *height_out) {
            *height_out = desired;
        }
    }

    (void)GdiUnloadFont(&font);
}

/*
 * Return whether one client-area point is inside the centered OK button.
 *
 * @param state Active message-box state.
 * @param x Local X coordinate.
 * @param y Local Y coordinate.
 * @return Non-zero when the point falls inside the OK button.
 */
static int window_message_box_hit_ok(const WindowMessageBoxState* state, long x, long y) {
    unsigned long button_x;

    if (state == 0 || state->width < WINDOW_MESSAGEBOX_BUTTON_WIDTH || x < 0L || y < 0L) {
        return 0;
    }

    button_x = (state->width - WINDOW_MESSAGEBOX_BUTTON_WIDTH) / 2UL;
    return (unsigned long)x >= button_x
        && (unsigned long)x < (button_x + WINDOW_MESSAGEBOX_BUTTON_WIDTH)
        && (unsigned long)y >= WINDOW_MESSAGEBOX_BUTTON_Y
        && (unsigned long)y < (WINDOW_MESSAGEBOX_BUTTON_Y + WINDOW_MESSAGEBOX_BUTTON_HEIGHT);
}

/*
 * Paint the first-version message box using only window.dll and GDI primitives.
 *
 * @param state Active message-box state.
 * @return Nothing.
 */
static void window_paint_message_box(WindowMessageBoxState* state) {
    RosGdiSurface surface;
    RosGdiFont font;
    unsigned long body_height;
    unsigned long panel_color;
    unsigned long frame_color;
    unsigned long text_color;
    unsigned long button_x;
    unsigned long button_fill;

    if (state == 0 || state->hwnd == 0UL) {
        return;
    }

    if (GdiGetWindowSurface(state->hwnd, &surface) < 0L) {
        return;
    }

    panel_color = (state->flags & ROS_MESSAGEBOX_FLAG_ERROR) != 0UL ? WINDOW_MESSAGEBOX_PANEL_ERROR : WINDOW_MESSAGEBOX_PANEL_NORMAL;
    frame_color = (state->flags & ROS_MESSAGEBOX_FLAG_ERROR) != 0UL ? WINDOW_MESSAGEBOX_FRAME_ERROR : WINDOW_MESSAGEBOX_FRAME_NORMAL;
    text_color = (state->flags & ROS_MESSAGEBOX_FLAG_ERROR) != 0UL ? WINDOW_MESSAGEBOX_TEXT_ERROR : WINDOW_MESSAGEBOX_TEXT_NORMAL;
    body_height = state->height > 74UL ? state->height - 74UL : 94UL;
    button_x = state->width > WINDOW_MESSAGEBOX_BUTTON_WIDTH
        ? (state->width - WINDOW_MESSAGEBOX_BUTTON_WIDTH) / 2UL
        : 0UL;
    button_fill = state->pointer_down_ok ? WINDOW_MESSAGEBOX_BUTTON_PRESSED : WINDOW_MESSAGEBOX_BUTTON_BOTTOM;

    (void)GdiFillSurfaceRect(&surface, 0UL, 0UL, surface.width, surface.height, (state->flags & ROS_MESSAGEBOX_FLAG_ERROR) != 0UL ? WINDOW_MESSAGEBOX_BG_ERROR : WINDOW_MESSAGEBOX_BG_NORMAL);
    if (surface.width > (WINDOW_MESSAGEBOX_MIN_MARGIN * 2UL) && surface.height > 68UL) {
        (void)GdiFillSurfaceRect(&surface,
            WINDOW_MESSAGEBOX_MIN_MARGIN,
            WINDOW_MESSAGEBOX_MIN_MARGIN,
            surface.width - (WINDOW_MESSAGEBOX_MIN_MARGIN * 2UL),
            body_height,
            panel_color);
        (void)GdiFillSurfaceRect(&surface, WINDOW_MESSAGEBOX_MIN_MARGIN, WINDOW_MESSAGEBOX_MIN_MARGIN, surface.width - (WINDOW_MESSAGEBOX_MIN_MARGIN * 2UL), 1UL, frame_color);
        (void)GdiFillSurfaceRect(&surface, WINDOW_MESSAGEBOX_MIN_MARGIN, WINDOW_MESSAGEBOX_MIN_MARGIN, 1UL, body_height, frame_color);
        (void)GdiFillSurfaceRect(&surface, WINDOW_MESSAGEBOX_MIN_MARGIN, WINDOW_MESSAGEBOX_MIN_MARGIN + body_height - 1UL, surface.width - (WINDOW_MESSAGEBOX_MIN_MARGIN * 2UL), 1UL, frame_color);
        (void)GdiFillSurfaceRect(&surface, surface.width - WINDOW_MESSAGEBOX_MIN_MARGIN - 1UL, WINDOW_MESSAGEBOX_MIN_MARGIN, 1UL, body_height, frame_color);
    }

    (void)GdiFillSurfaceRect(&surface, button_x, WINDOW_MESSAGEBOX_BUTTON_Y, WINDOW_MESSAGEBOX_BUTTON_WIDTH, WINDOW_MESSAGEBOX_BUTTON_HEIGHT, button_fill);
    (void)GdiFillSurfaceRect(&surface, button_x, WINDOW_MESSAGEBOX_BUTTON_Y, WINDOW_MESSAGEBOX_BUTTON_WIDTH, 1UL, WINDOW_MESSAGEBOX_BUTTON_FRAME);
    (void)GdiFillSurfaceRect(&surface, button_x, WINDOW_MESSAGEBOX_BUTTON_Y, 1UL, WINDOW_MESSAGEBOX_BUTTON_HEIGHT, WINDOW_MESSAGEBOX_BUTTON_FRAME);
    (void)GdiFillSurfaceRect(&surface, button_x, WINDOW_MESSAGEBOX_BUTTON_Y + WINDOW_MESSAGEBOX_BUTTON_HEIGHT - 1UL, WINDOW_MESSAGEBOX_BUTTON_WIDTH, 1UL, WINDOW_MESSAGEBOX_BUTTON_FRAME);
    (void)GdiFillSurfaceRect(&surface, button_x + WINDOW_MESSAGEBOX_BUTTON_WIDTH - 1UL, WINDOW_MESSAGEBOX_BUTTON_Y, 1UL, WINDOW_MESSAGEBOX_BUTTON_HEIGHT, WINDOW_MESSAGEBOX_BUTTON_FRAME);

    window_zero_memory(&font, sizeof(font));
    if (GdiLoadFont(0, 14UL, &font) >= 0L) {
        (void)GdiDrawTextSurface(&surface, &font, WINDOW_MESSAGEBOX_TEXT_X, WINDOW_MESSAGEBOX_TEXT_Y, state->message != 0 ? state->message : "", text_color);
        (void)GdiDrawTextSurface(&surface, &font, button_x + 30UL, WINDOW_MESSAGEBOX_BUTTON_Y + 6UL, "OK", WINDOW_MESSAGEBOX_TEXT_NORMAL);
        (void)GdiUnloadFont(&font);
    }

    (void)GdiReleaseWindowSurface(state->hwnd);
    (void)GdiInvalidateRect(state->hwnd, 0UL, 0UL, surface.width, surface.height);
}

/*
 * Handle the built-in message-box class.
 *
 * @param hwnd Message-box window handle.
 * @param message Window message identifier.
 * @param wParam First payload word.
 * @param lParam Second payload word.
 * @return Zero.
 */
static long window_message_box_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    WindowMessageBoxState* state = g_window_active_message_box;
    long x = 0L;
    long y = 0L;

    (void)wParam;
    (void)hwnd;

    if (state == 0) {
        return 0L;
    }

    switch (message) {
    case WM_CREATE:
    case WM_PAINT:
        window_paint_message_box(state);
        return 0L;
    case WM_KEYDOWN:
        if (wParam == 13UL) {
            state->result = ROS_MESSAGEBOX_RESULT_OK;
            state->result_ready = 1;
        }
        else if (wParam == 27UL) {
            state->result = ROS_MESSAGEBOX_RESULT_CANCEL;
            state->result_ready = 1;
        }
        return 0L;
    case WM_LBUTTONDOWN:
        WindowUnpackSignedPair(lParam, &x, &y);
        if (window_message_box_hit_ok(state, x, y)) {
            state->pointer_down_ok = 1;
            window_paint_message_box(state);
        }
        return 0L;
    case WM_LBUTTONUP:
        WindowUnpackSignedPair(lParam, &x, &y);
        if (state->pointer_down_ok && window_message_box_hit_ok(state, x, y)) {
            state->result = ROS_MESSAGEBOX_RESULT_OK;
            state->result_ready = 1;
        }
        state->pointer_down_ok = 0;
        window_paint_message_box(state);
        return 0L;
    case WM_CLOSE:
    case WM_DESTROY:
        if (!state->result_ready) {
            state->result = ROS_MESSAGEBOX_RESULT_CANCEL;
            state->result_ready = 1;
        }
        return 0L;
    default:
        return 0L;
    }
}

/*
 * Show one synchronous built-in message box.
 *
 * @param params Caller-owned message-box description.
 * @return `ROS_MESSAGEBOX_RESULT_OK` on acknowledgement, or another non-positive value on failure.
 */
long MessageBoxShow(const MessageBoxParams* params) {
    WindowCreateParams create_params;
    WindowMessageBoxState state;
    WindowHandleRegistration* owner_slot = 0;
    MSG message;
    unsigned long display_width = 800UL;
    unsigned long display_height = 600UL;
    RosKernelGuiDisplayInfo info;
    long get_result;
    long status;

    if (g_window_active_message_box != 0) {
        return ROS_MESSAGEBOX_RESULT_CANCEL;
    }
    if (CreateWindowClass(WINDOW_MESSAGEBOX_CLASS_NAME, window_message_box_wndproc) < 0L) {
        return ROS_MESSAGEBOX_RESULT_CANCEL;
    }

    window_zero_memory(&state, sizeof(state));
    state.owner = params != 0 ? params->owner : 0UL;
    state.title = (params != 0 && params->title != 0 && params->title[0] != '\0')
        ? params->title
        : (((params != 0 ? params->flags : 0UL) & ROS_MESSAGEBOX_FLAG_ERROR) != 0UL ? WINDOW_MESSAGEBOX_DEFAULT_ERROR_TITLE : WINDOW_MESSAGEBOX_DEFAULT_TITLE);
    state.message = (params != 0 && params->message != 0) ? params->message : "";
    state.flags = params != 0 ? params->flags : ROS_MESSAGEBOX_FLAG_NONE;
    state.width = WINDOW_MESSAGEBOX_DEFAULT_WIDTH;
    state.height = WINDOW_MESSAGEBOX_DEFAULT_HEIGHT;
    window_measure_message_box_text(state.message, &state.height);

    window_zero_memory(&info, sizeof(info));
    status = controlGui(ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO, (unsigned long)&info);
    if (status >= 0L && info.version == ROS_KERNEL_GUI_DISPLAY_INFO_VERSION && info.width != 0U && info.height != 0U) {
        display_width = info.width;
        display_height = info.height;
    }

    if (state.owner != 0UL) {
        owner_slot = window_find_handle(state.owner);
    }

    create_params.class_name = WINDOW_MESSAGEBOX_CLASS_NAME;
    create_params.title = state.title;
    create_params.parent = 0UL;
    if (owner_slot != 0 && owner_slot->width != 0UL && owner_slot->height != 0UL) {
        create_params.x = owner_slot->x + (long)((owner_slot->width > state.width) ? ((owner_slot->width - state.width) / 2UL) : 0UL);
        create_params.y = owner_slot->y + (long)((owner_slot->height > state.height) ? ((owner_slot->height - state.height) / 2UL) : 0UL);
    }
    else {
        create_params.x = (long)((display_width > state.width) ? ((display_width - state.width) / 2UL) : 0UL);
        create_params.y = (long)((display_height > state.height) ? ((display_height - state.height) / 2UL) : 0UL);
    }
    if (create_params.x < 0L) {
        create_params.x = 0L;
    }
    if (create_params.y < 0L) {
        create_params.y = 0L;
    }
    create_params.width = state.width;
    create_params.height = state.height;
    create_params.style = ROS_WINDOW_STYLE_VISIBLE | ROS_WINDOW_STYLE_DECORATED | ROS_WINDOW_STYLE_TOPMOST;

    g_window_active_message_box = &state;
    state.hwnd = CreateWindowEx(&create_params);
    if (state.hwnd == 0UL) {
        g_window_active_message_box = 0;
        return ROS_MESSAGEBOX_RESULT_CANCEL;
    }

    (void)SetForegroundWindow(state.hwnd);
    while (!state.result_ready) {
        get_result = GetMessage(&message);
        if (get_result <= 0L) {
            state.result = ROS_MESSAGEBOX_RESULT_CANCEL;
            state.result_ready = 1;
            break;
        }

        if (state.owner != 0UL
            && message.hwnd != state.hwnd
            && window_handle_is_descendant_of(message.hwnd, state.owner)
            && window_message_is_modal_blocked(message.message)) {
            continue;
        }

        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    if (state.hwnd != 0UL) {
        (void)DestroyWindow(state.hwnd);
    }
    if (state.owner != 0UL) {
        (void)SetForegroundWindow(state.owner);
    }
    g_window_active_message_box = 0;
    return state.result;
}

/*
 * Show one basic built-in notification message box.
 *
 * @param title Optional dialog title.
 * @param message Optional message body.
 * @return `ROS_MESSAGEBOX_RESULT_OK` on acknowledgement, or another non-positive value on failure.
 */
long MessageBox(const char* title, const char* message) {
    MessageBoxParams params;

    params.owner = 0UL;
    params.title = title;
    params.message = message;
    params.flags = ROS_MESSAGEBOX_FLAG_NONE;
    return MessageBoxShow(&params);
}

/*
 * Show one error-style built-in message box.
 *
 * @param title Optional dialog title.
 * @param message Optional message body.
 * @return `ROS_MESSAGEBOX_RESULT_OK` on acknowledgement, or another non-positive value on failure.
 */
long MessageBoxError(const char* title, const char* message) {
    MessageBoxParams params;

    params.owner = 0UL;
    params.title = title;
    params.message = message;
    params.flags = ROS_MESSAGEBOX_FLAG_ERROR;
    return MessageBoxShow(&params);
}

/*
 * Queue one background file read through the window.dll asset worker.
 *
 * This helper gives GUI callers one callback-style file loader that does not
 * monopolize the UI owner thread during startup or incremental asset refreshes.
 *
 * @param path DOS-style file path to read.
 * @param success_callback Completion callback for successful reads.
 * @param error_callback Completion callback for failed reads.
 * @param context Opaque caller-owned cookie forwarded to callbacks.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadFileAssetAsync(
    const char* path,
    WindowAssetLoadSuccessCallback success_callback,
    WindowAssetLoadErrorCallback error_callback,
    void* context) {
    return window_queue_async_asset_request(
        WINDOW_ASSET_REQUEST_KIND_FILE,
        path,
        0,
        success_callback,
        error_callback,
        context,
        0UL,
        0UL);
}

/*
 * Queue one background file read and notify one owner window on completion.
 *
 * @param path DOS-style file path to read.
 * @param hwnd Owner window that should receive the completion message.
 * @param message Window message identifier posted when the request completes.
 * @param context Opaque caller-owned cookie copied into the completion record.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadFileAssetAsyncNotify(const char* path, HWND hwnd, unsigned long message, void* context) {
    return window_queue_async_asset_request(
        WINDOW_ASSET_REQUEST_KIND_FILE,
        path,
        0,
        0,
        0,
        context,
        hwnd,
        message);
}

/*
 * Queue one background DLL-section read through the window.dll asset worker.
 *
 * Embedded assets travel through a named image section inside another DLL, so
 * this helper captures both the module path and the short section identifier in
 * the async request and resolves them later on the background worker.
 *
 * @param module_path DOS-style DLL path that owns the embedded asset.
 * @param section_name Null-terminated section name, up to 8 characters.
 * @param success_callback Completion callback for successful reads.
 * @param error_callback Completion callback for failed reads.
 * @param context Opaque caller-owned cookie forwarded to callbacks.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadModuleSectionAssetAsync(
    const char* module_path,
    const char* section_name,
    WindowAssetLoadSuccessCallback success_callback,
    WindowAssetLoadErrorCallback error_callback,
    void* context) {
    return window_queue_async_asset_request(
        WINDOW_ASSET_REQUEST_KIND_MODULE_SECTION,
        module_path,
        section_name,
        success_callback,
        error_callback,
        context,
        0UL,
        0UL);
}

/*
 * Queue one background DLL-section read and notify one owner window on completion.
 *
 * @param module_path DOS-style DLL path that owns the embedded asset.
 * @param section_name Null-terminated section name, up to 8 characters.
 * @param hwnd Owner window that should receive the completion message.
 * @param message Window message identifier posted when the request completes.
 * @param context Opaque caller-owned cookie copied into the completion record.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadModuleSectionAssetAsyncNotify(
    const char* module_path,
    const char* section_name,
    HWND hwnd,
    unsigned long message,
    void* context) {
    return window_queue_async_asset_request(
        WINDOW_ASSET_REQUEST_KIND_MODULE_SECTION,
        module_path,
        section_name,
        0,
        0,
        context,
        hwnd,
        message);
}

/*
 * Claim one queued asset completion published by the worker thread.
 *
 * @param request_id Request identifier carried by the posted completion message.
 * @param completion Receives the copied completion record.
 * @return Zero on success, or a negative status code when the result is absent.
 */
long ReceiveAssetCompletion(unsigned long request_id, WindowAssetCompletion* completion) {
    WindowAsyncAssetCompletionNode* current;

    if (request_id == 0UL || completion == 0) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    window_zero_memory(completion, sizeof(*completion));
    window_queue_lock();
    current = g_window_async_asset_completion_head;
    while (current != 0) {
        if (current->completion.request_id == request_id) {
            if (current->prev != 0) {
                current->prev->next = current->next;
            }
            else {
                g_window_async_asset_completion_head = current->next;
            }
            if (current->next != 0) {
                current->next->prev = current->prev;
            }
            else {
                g_window_async_asset_completion_tail = current->prev;
            }
            current->next = 0;
            current->prev = 0;
            break;
        }
        current = current->next;
    }
    window_queue_unlock();

    if (current == 0) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    *completion = current->completion;
    window_heap_free(current);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Release one asset buffer previously returned by an async success callback.
 *
 * Callers receive copied bytes from the shared GUI heap, so this helper keeps
 * buffer disposal aligned with the allocator that produced the asset.
 *
 * @param bytes Buffer previously returned by the async asset loader.
 * @return Nothing.
 */
void FreeAssetBuffer(void* bytes) {
    if (bytes != 0) {
        window_heap_free(bytes);
    }
}

/*
 * Park the shared async asset worker before `window.dll` unloads.
 *
 * The worker thread lives for the lifetime of the process, but EXE shutdown can
 * unload `window.dll` before the process itself is destroyed. Waiting for the
 * worker to acknowledge a shutdown request keeps that thread inside a blocking
 * kernel wait instead of executing unmapped DLL code after process detach.
 *
 * @return Zero on success, or a negative status code on timeout/failure.
 */
int window_entry(void* image_base, U32 reason) {
    (void)image_base;

    if (reason == DLL_REASON_PROCESS_ATTACH) {
        window_reset_state();
        return 1;
    }
    if (reason == DLL_REASON_PROCESS_DETACH) {
        (void)window_shutdown_async_asset_worker();
        window_reset_state();
        return 1;
    }

    return 1;
}
