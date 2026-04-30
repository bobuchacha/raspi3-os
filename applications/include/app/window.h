#ifndef ROS_APP_WINDOW_H
#define ROS_APP_WINDOW_H

#include "user_runtime.h"
#include "app/user_ipc.h"

#define ROS_WINDOW_SERVER_NAME "gwes"
#define ROS_WINDOW_CLIENT_MODULE_NAME "window.dll"
#define ROS_WINDOW_SERVER_SHARED_STATE_NAME "ros.window-server.state"
#define ROS_WINDOW_SERVER_SHARED_STATE_VERSION 1UL

#define ROS_WINDOW_SERVER_PROTOCOL 0x47574553UL
#define ROS_WINDOW_SERVER_KIND_REGISTER_CLASS 1UL
#define ROS_WINDOW_SERVER_KIND_REGISTER_CLASS_ACK 2UL
#define ROS_WINDOW_SERVER_KIND_CREATE_WINDOW 3UL
#define ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY 4UL
#define ROS_WINDOW_SERVER_KIND_DELIVER_MESSAGE 5UL
#define ROS_WINDOW_SERVER_KIND_PROCESS_QUIT 6UL
#define ROS_WINDOW_SERVER_KIND_INVALIDATE_WINDOW 7UL
#define ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR 8UL
#define ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR_REPLY 9UL
#define ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS 10UL
#define ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS_REPLY 11UL
#define ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW 12UL
#define ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW_REPLY 13UL
#define ROS_WINDOW_SERVER_KIND_SET_TIMER 14UL
#define ROS_WINDOW_SERVER_KIND_SET_TIMER_REPLY 15UL
#define ROS_WINDOW_SERVER_KIND_KILL_TIMER 16UL
#define ROS_WINDOW_SERVER_KIND_KILL_TIMER_REPLY 17UL
#define ROS_WINDOW_SERVER_KIND_POST_FOREGROUND_WINDOW 18UL
#define ROS_WINDOW_SERVER_KIND_DESTROY_WINDOW 19UL
#define ROS_WINDOW_SERVER_KIND_DESTROY_WINDOW_REPLY 20UL
#define ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU 21UL
#define ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU_REPLY 22UL

#define ROS_WINDOW_CLASS_NAME_MAX ROS_USER_IPC_TEXT_MAX
#define ROS_WINDOW_TITLE_MAX ROS_USER_IPC_TEXT2_MAX
#define ROS_WINDOW_CURSOR_PATH_MAX ROS_USER_IPC_TEXT_MAX

#define ROS_WINDOW_CURSOR_DEFAULT_PATH "C:\\cursors\\arrow.cur32"
#define ROS_WINDOW_CURSOR_CROSSHAIR_PATH "C:\\cursors\\crosshair.cur32"
#define ROS_WINDOW_CURSOR_BUSY_PATH "C:\\cursors\\busy.cur32"
#define ROS_WINDOW_ASSET_SECTION_NAME_MAX 9UL

#define ROS_WINDOW_MENU_SHARED_NAME_MAX ROS_USER_IPC_TEXT_MAX
#define ROS_WINDOW_MENU_MODEL_VERSION 1UL
#define ROS_WINDOW_MENU_MAX_MENUS 8UL
#define ROS_WINDOW_MENU_MAX_ITEMS 32UL
#define ROS_WINDOW_MENU_ITEM_TEXT_MAX 64UL
#define ROS_WINDOW_MENU_INVALID_INDEX 0xFFFFFFFFUL

#define ROS_WINDOW_STYLE_VISIBLE (1UL << 0)
#define ROS_WINDOW_STYLE_CHILD (1UL << 1)
#define ROS_WINDOW_STYLE_DECORATED (1UL << 2)
#define ROS_WINDOW_STYLE_DISABLED (1UL << 3)
#define ROS_WINDOW_STYLE_BORDER (1UL << 4)
#define ROS_WINDOW_STYLE_TOPMOST (1UL << 5)
#define ROS_WINDOW_STYLE_SYSTEM_UI (1UL << 6)
#define ROS_WINDOW_STYLE_FULLSCREEN (1UL << 7)

#define WM_NULL 0UL
#define WM_CREATE 1UL
#define WM_DESTROY 2UL
#define WM_REPAINT 3UL
#define WM_MOUSEMOVE 4UL
#define WM_MOUSELEAVE 5UL
#define WM_LBUTTONDOWN 6UL
#define WM_LBUTTONUP 7UL
#define WM_MOUSECLICKED 8UL
#define WM_KEYDOWN 9UL
#define WM_KEYUP 10UL
#define WM_CLOSE 11UL
#define WM_MOVE 12UL
#define WM_SIZE 13UL
#define WM_CHANGED 14UL
#define WM_QUIT 15UL
#define WM_TIMER 16UL
#define WM_PAINT 17UL
#define WM_COMMAND 18UL
#define WM_RBUTTONDOWN 19UL
#define WM_RBUTTONUP 20UL
#define WM_USER 1024UL

#define ROS_MENU_ITEM_FLAG_SEPARATOR (1UL << 0)
#define ROS_MENU_ITEM_FLAG_DISABLED (1UL << 1)
#define ROS_MENU_ITEM_FLAG_DEFAULT (1UL << 2)
#define ROS_MENU_ITEM_FLAG_SUBMENU (1UL << 3)

#define ROS_MENU_TRACK_RETURNCMD (1UL << 0)

#define ROS_MESSAGEBOX_FLAG_NONE 0UL
#define ROS_MESSAGEBOX_FLAG_ERROR 1UL

#define ROS_MESSAGEBOX_RESULT_CANCEL 0L
#define ROS_MESSAGEBOX_RESULT_OK 1L

#define ROS_WINDOW_ASSET_TIMEOUT_MSEC 5000UL
#define ROS_WINDOW_ASSET_STATUS_TIMEOUT (-1001L)

typedef unsigned long HWND;
typedef unsigned long HMENU;
typedef long LRESULT;
typedef LRESULT(*WNDPROC)(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);

typedef struct RosWindowMenuDescriptor {
    unsigned long first_item;
    unsigned long item_count;
} RosWindowMenuDescriptor;

typedef struct RosWindowMenuItem {
    unsigned long command_id;
    unsigned long flags;
    unsigned long hotkey;
    unsigned long submenu_index;
    char text[ROS_WINDOW_MENU_ITEM_TEXT_MAX];
} RosWindowMenuItem;

typedef struct RosWindowMenuModel {
    unsigned long version;
    unsigned long root_menu_index;
    unsigned long menu_count;
    unsigned long item_count;
    RosWindowMenuDescriptor menus[ROS_WINDOW_MENU_MAX_MENUS];
    RosWindowMenuItem items[ROS_WINDOW_MENU_MAX_ITEMS];
} RosWindowMenuModel;

typedef struct WindowCreateParams {
    const char* class_name;
    const char* title;
    HWND parent;
    long x;
    long y;
    unsigned long width;
    unsigned long height;
    unsigned long style;
} WindowCreateParams;

typedef struct MessageBoxParams {
    HWND owner;
    const char* title;
    const char* message;
    unsigned long flags;
} MessageBoxParams;

/*
 * Callback invoked when one async asset load completes successfully.
 *
 * `window.dll` runs the callback on its dedicated background loader thread so
 * disk and DLL I/O never block the caller's UI message pump. The byte buffer
 * remains valid after the callback returns and should be released with
 * `FreeAssetBuffer` once the consumer is done with it. `path` and
 * `resource_name` point at request-owned storage and are only guaranteed to
 * stay valid for the duration of the callback itself.
 *
 * @param request_id Stable request identifier returned by the enqueue call.
 * @param bytes Heap-backed buffer containing the loaded asset bytes, or NULL for an empty asset.
 * @param size Byte count stored in `bytes`.
 * @param path File path or DLL path that sourced the asset.
 * @param resource_name NULL for file loads, or the DLL section name for embedded resources.
 * @param context Opaque caller-owned cookie forwarded from the enqueue call.
 * @return Nothing.
 */
typedef void (*WindowAssetLoadSuccessCallback)(
    unsigned long request_id,
    const void* bytes,
    unsigned long size,
    const char* path,
    const char* resource_name,
    void* context);

/*
 * Callback invoked when one async asset load fails.
 *
 * Failures are reported on the same background loader thread so the caller can
 * log, retry, or post a wake-up message back to its UI owner without waiting on
 * synchronous file or DLL work.
 *
 * @param request_id Stable request identifier returned by the enqueue call.
 * @param status Negative failure status produced while loading the asset.
 * @param path File path or DLL path that failed.
 * @param resource_name NULL for file loads, or the DLL section name for embedded resources.
 * @param context Opaque caller-owned cookie forwarded from the enqueue call.
 * @return Nothing.
 */
typedef void (*WindowAssetLoadErrorCallback)(
    unsigned long request_id,
    long status,
    const char* path,
    const char* resource_name,
    void* context);

/*
 * One async asset completion record claimed by the UI thread.
 *
 * Message-based asset delivery lets a worker thread do the blocking file or
 * DLL read while the owner thread keeps its normal `GetMessage` /
 * `DispatchMessage` pump responsive. The completion record is copied out of
 * window.dll's per-process queue when the UI thread calls
 * `ReceiveAssetCompletion` after its posted notification arrives.
 *
 * @param request_id Stable request identifier returned by the enqueue call.
 * @param status Zero on success, or a negative status code on failure.
 * @param bytes Heap-backed buffer containing loaded bytes on success, or NULL.
 * @param size Byte count stored in `bytes`.
 * @param context Opaque caller-owned cookie forwarded from the enqueue call.
 * @param path Source file or DLL path captured at enqueue time.
 * @param resource_name Empty string for file loads, or the DLL section name.
 */
typedef struct WindowAssetCompletion {
    unsigned long request_id;
    long status;
    void* bytes;
    unsigned long size;
    void* context;
    char path[ROS_USER_IPC_TEXT_MAX];
    char resource_name[ROS_WINDOW_ASSET_SECTION_NAME_MAX];
} WindowAssetCompletion;

typedef struct MSG {
    HWND hwnd;
    unsigned long message;
    unsigned long wParam;
    unsigned long lParam;
} MSG;

typedef struct RosWindowServerSharedState {
    unsigned long version;
    long pid;
    char name[32];
} RosWindowServerSharedState;

#if defined(ROS_WINDOWKIT_EXPORTS) && !defined(ROS_BUILDING_WINDOW_DLL)
#error "ROS_WINDOWKIT_EXPORTS is reserved for the dedicated window.dll wrapper build"
#endif

/*
 * Copy one small ASCII string without depending on hosted libc helpers.
 *
 * The window runtime shares this helper between the boot supervisor and the
 * client DLLs so the published GWES registry block stays self-contained.
 *
 * @param destination Caller-owned character buffer.
 * @param capacity Destination buffer capacity in bytes.
 * @param source Null-terminated source string.
 * @return Nothing.
 */
static inline void WindowCopyText(char* destination, unsigned long capacity, const char* source) {
    unsigned long index = 0UL;

    if (!destination || capacity == 0UL) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    while (source[index] != '\0' && (index + 1UL) < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

/*
 * Open the shared GWES publication block.
 *
 * The boot supervisor passes a non-zero size so the block is created on first
 * use, while consumers pass zero so lookup stays read-only and cannot invent a
 * fake window-server record on their own.
 *
 * @param requested_size Shared object size, or zero to require an existing one.
 * @return Mapped shared-state pointer, or NULL when the mapping failed.
 */
static inline RosWindowServerSharedState* WindowServerSharedState(unsigned long requested_size) {
    void* address = 0;

    if (acquireSharedMemoryRegion(ROS_WINDOW_SERVER_SHARED_STATE_NAME, requested_size, &address) < 0) {
        return 0;
    }

    return (RosWindowServerSharedState*)address;
}

/*
 * Initialize the shared GWES publication block.
 *
 * The version/name pair lets readers reject stale or unrelated shared objects
 * instead of trusting any positive PID found at the expected name.
 *
 * @param state Shared-state mapping to initialize.
 * @return Nothing.
 */
static inline void WindowServerInitializeSharedState(RosWindowServerSharedState* state) {
    if (!state) {
        return;
    }

    state->version = ROS_WINDOW_SERVER_SHARED_STATE_VERSION;
    state->pid = ROS_USER_IPC_STATUS_NOT_FOUND;
    WindowCopyText(state->name, sizeof(state->name), ROS_WINDOW_SERVER_NAME);
}

/*
 * Read the GWES PID published by the boot supervisor.
 *
 * Client DLLs prefer this explicit publication path over task-table scans so a
 * GUI process can still find GWES when task enumeration is incomplete or slow.
 *
 * @return Published GWES PID on success, or `ROS_USER_IPC_STATUS_NOT_FOUND`.
 */
static inline long WindowServerPublishedPid(void) {
    RosWindowServerSharedState* state = WindowServerSharedState(0UL);

    if (!state) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (state->version != ROS_WINDOW_SERVER_SHARED_STATE_VERSION) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (!userIpcTaskNameMatches(state->name, ROS_WINDOW_SERVER_NAME)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (state->pid < 0) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    return state->pid;
}

/*
 * Pack two signed 32-bit values into one window-message payload word.
 *
 * The current AArch64 userspace ABI carries `lParam` as one 64-bit scalar, so
 * coordinate pairs travel as X in the upper 32 bits and Y in the lower 32
 * bits. Keeping this helper in the shared header prevents clients from
 * reintroducing legacy 16-bit `LOWORD`/`HIWORD` decoding.
 *
 * @param first Signed value stored in the upper 32 bits.
 * @param second Signed value stored in the lower 32 bits.
 * @return Packed payload word suitable for `lParam`.
 */
static inline unsigned long WindowPackSignedPair(long first, long second) {
    return (((unsigned long)(unsigned int)first) << 32) | (unsigned long)(unsigned int)second;
}

/*
 * Decode two signed 32-bit values from one packed window-message payload.
 *
 * @param packed Combined payload previously created by `WindowPackSignedPair`.
 * @param first Receives the upper signed 32-bit value when non-null.
 * @param second Receives the lower signed 32-bit value when non-null.
 * @return Nothing.
 */
static inline void WindowUnpackSignedPair(unsigned long packed, long* first, long* second) {
    if (first) {
        *first = (long)(int)((packed >> 32) & 0xFFFFFFFFUL);
    }
    if (second) {
        *second = (long)(int)(packed & 0xFFFFFFFFUL);
    }
}

/*
 * Build one stable shared-memory object name for the current popup-menu payload.
 *
 * The first menu transport keeps the serialized item list in one small named
 * shared block so `window.dll` can marshal the menu model to GWES without
 * expanding the fixed four-word window IPC packet format.
 *
 * @param destination Caller-owned output buffer.
 * @param capacity Output buffer capacity in bytes.
 * @param owner Stable owner window handle for the popup session.
 * @param menu Process-local menu handle being tracked.
 * @return Nothing.
 */
static inline void WindowBuildMenuSharedName(char* destination, unsigned long capacity, HWND owner, HMENU menu) {
    char* cursor;

    if (!destination || capacity == 0UL) {
        return;
    }

    destination[0] = '\0';
    cursor = appendText(destination, "ros.window.menu.");
    cursor = appendUnsignedLong(cursor, (unsigned long)owner);
    cursor = appendText(cursor, ".");
    cursor = appendUnsignedLong(cursor, (unsigned long)menu);
    destination[capacity - 1UL] = '\0';
}

#if defined(ROS_WINDOWKIT_EXPORTS) || defined(ROS_WINDOW_SERVER_ONLY)

/*
 * Register one local window class and mirror that registration to GWES.
 *
 * @param class_name Stable class name used by later `CreateWindow` calls.
 * @param proc Local window procedure for this class.
 * @return Zero on success, or a negative status code on failure.
 */
long CreateWindowClass(const char* class_name, WNDPROC proc);

/*
 * Request one server-owned window instance from GWES with explicit parent,
 * placement, and style metadata.
 *
 * @param params Caller-owned creation parameters.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND CreateWindowEx(const WindowCreateParams* params);

/*
 * Request one server-owned window instance from GWES.
 *
 * @param class_name Previously registered class name.
 * @param title Window title copied into the server-side record.
 * @return Non-zero window handle on success, or zero on failure.
 */
HWND CreateWindow(const char* class_name, const char* title);

/*
 * Destroy one live window owned by the current process.
 *
 * This explicit teardown path is required for modal helpers like the built-in
 * message box because GWES otherwise keeps windows alive until the whole
 * process quits.
 *
 * @param hwnd Stable window handle previously returned by `CreateWindowEx`.
 * @return Zero on success, or a negative status code on failure.
 */
long DestroyWindow(HWND hwnd);

/*
 * Allocate one process-local popup-menu description handle.
 *
 * The initial menu transport keeps menu storage in `window.dll` and marshals a
 * snapshot into GWES only when `TrackPopupMenu` begins a live popup session.
 *
 * @return Non-zero menu handle on success, or zero on failure.
 */
HMENU CreateMenu(void);

/*
 * Release one process-local popup-menu description handle.
 *
 * @param menu Menu handle previously returned by `CreateMenu`.
 * @return Zero on success, or a negative status code on failure.
 */
long DestroyMenu(HMENU menu);

/*
 * Append one command item to a process-local menu description.
 *
 * @param menu Target process-local menu handle.
 * @param command_id Command identifier returned on selection.
 * @param flags Item flags such as disabled/default.
 * @param text Visible menu caption.
 * @param hotkey Optional ASCII mnemonic that selects the item while open.
 * @return Zero on success, or a negative status code on failure.
 */
long AppendMenuItem(HMENU menu, unsigned long command_id, unsigned long flags, const char* text, unsigned long hotkey);

/*
 * Append one submenu row that opens another process-local popup definition.
 *
 * The submenu handle remains process-local inside `window.dll` until
 * `TrackPopupMenu` serializes the full rooted menu tree into GWES.
 *
 * @param menu Parent menu that receives the submenu row.
 * @param submenu Child menu handle that should open from this row.
 * @param flags Item flags such as disabled/default.
 * @param text Visible submenu caption.
 * @param hotkey Optional ASCII mnemonic that focuses or opens the submenu.
 * @return Zero on success, or a negative status code on failure.
 */
long AppendSubMenu(HMENU menu, HMENU submenu, unsigned long flags, const char* text, unsigned long hotkey);

/*
 * Append one visual separator row to a process-local menu description.
 *
 * @param menu Target process-local menu handle.
 * @return Zero on success, or a negative status code on failure.
 */
long AppendMenuSeparator(HMENU menu);

/*
 * Ask GWES to show and track one authoritative popup menu session.
 *
 * When `ROS_MENU_TRACK_RETURNCMD` is set, the selected command identifier is
 * returned directly. Otherwise `window.dll` posts `WM_COMMAND` back to `owner`
 * and returns one on success, zero on cancellation, or a negative error code.
 *
 * @param menu Process-local menu handle to marshal into GWES.
 * @param flags Popup tracking flags.
 * @param x Desktop X coordinate for the popup origin.
 * @param y Desktop Y coordinate for the popup origin.
 * @param owner Local owner window that receives `WM_COMMAND` when requested.
 * @return Command identifier, one, zero, or a negative status code.
 */
long TrackPopupMenu(HMENU menu, unsigned long flags, long x, long y, HWND owner);

/*
 * Update one window's preferred cursor asset.
 *
 * Keeping this as a separate call lets clients change cursor policy after
 * creation without expanding the already-packed create-window wire format.
 * Passing NULL or an empty string clears the window-local override so GWES can
 * fall back to the parent chain or the default arrow cursor.
 *
 * @param hwnd Stable window handle previously returned by GWES.
 * @param cursor_path DOS-style path to one `.cur32` asset, or null to clear.
 * @return Zero on success, or a negative status code on failure.
 */
long SetWindowCursor(HWND hwnd, const char* cursor_path);

/*
 * Update one existing window's position and size.
 *
 * Top-level windows use desktop coordinates while child windows use their
 * parent's client coordinates, mirroring the same contract as `CreateWindow`.
 * The move and size notifications generated by GWES keep the local widget
 * caches and application procedures aligned with the server-side geometry.
 *
 * @param hwnd Stable window handle previously returned by GWES.
 * @param x New X position.
 * @param y New Y position.
 * @param width New width.
 * @param height New height.
 * @return Zero on success, or a negative status code on failure.
 */
long MoveWindow(HWND hwnd, long x, long y, unsigned long width, unsigned long height);

/*
 * Ask GWES to make one window the foreground root and raise its subtree.
 *
 * This intentionally accepts cross-process handles so shell chrome like the
 * explorer taskbar can activate application windows that it does not own.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long SetForegroundWindow(HWND hwnd);

/*
 * Queue one non-blocking foreground activation request for GWES.
 *
 * Shell-driven focus changes do not need an immediate acknowledgement, so this
 * variant lets Explorer stay responsive while GWES processes the activation on
 * its own window thread.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long PostSetForegroundWindow(HWND hwnd);

/*
 * Register or refresh one per-window timer owned by the current process.
 *
 * Passing a zero `timer_id` asks GWES to allocate a fresh identifier for the
 * target window. Passing a non-zero `timer_id` updates the existing timer when
 * present or creates a new timer with that identifier when absent.
 *
 * @param hwnd Target local window handle.
 * @param timer_id Existing timer identifier, or zero to allocate one.
 * @param interval_msec Timer period in milliseconds.
 * @return Stable timer identifier on success, or zero on failure.
 */
unsigned long SetTimer(HWND hwnd, unsigned long timer_id, unsigned long interval_msec);

/*
 * Cancel one previously registered per-window timer.
 *
 * @param hwnd Target local window handle.
 * @param timer_id Timer identifier previously returned by `SetTimer`.
 * @return Zero on success, or a negative status code on failure.
 */
long KillTimer(HWND hwnd, unsigned long timer_id);

/*
 * Deliver one message synchronously to the local window procedure.
 *
 * This mirrors Win32 `SendMessage` semantics for same-process dispatch: the
 * call does not return until the target procedure has finished handling the
 * message. Use this only when the caller needs immediate in-thread mutation or
 * a direct return value from the target procedure.
 *
 * @param hwnd Target local window handle.
 * @param message Window message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Window-procedure return value, or zero when the hwnd is unknown.
 */
LRESULT SendMessage(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);

/*
 * Queue one local message for the current process.
 *
 * @param hwnd Target local window handle.
 * @param message Window message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Zero on success, or a negative status code on failure.
 */
long PostMessage(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);

/*
 * Queue `WM_QUIT` so a running message pump can exit cleanly.
 *
 * @param exit_code Process-specific result code stored in `wParam`.
 * @return Zero on success, or a negative status code on failure.
 */
long PostQuitMessage(long exit_code);
/*
 * Receive one non-window IPC packet preserved by the window client DLL.
 *
 * The window DLL owns the process IPC demultiplexing path because it already
 * drains GWES traffic for `GetMessage`. Non-window packets are therefore
 * staged here so helper threads can consume shell-control traffic without
 * racing the UI message pump.
 *
 * @param packet Receives the next preserved IPC packet.
 * @param flags Zero for polling or `ROS_USER_IPC_RECEIVE_WAIT` for timed wait.
 * @return Zero on success, `ROS_USER_IPC_STATUS_BUSY` when no packet is ready,
 * or another negative status code on failure.
 */
long ReceiveProcessIpcMessage(UserIpcMessage* packet, unsigned long flags);

/*
 * Receive the next local or server-delivered window message.
 *
 * @param message Receives the next message.
 * @return One when a normal message was copied, zero when `WM_QUIT` was
 * received, or a negative status code on failure.
 */
long GetMessage(MSG* message);

/*
 * Placeholder keyboard translation step kept for Win32-style loop shape.
 *
 * @param message Message being translated.
 * @return Always zero for the current minimal transport.
 */
long TranslateMessage(const MSG* message);

/*
 * Invoke the registered local window procedure for one message.
 *
 * @param message Message to dispatch.
 * @return Window-procedure return value, or zero when the hwnd is unknown.
 */
LRESULT DispatchMessage(const MSG* message);

/*
 * Show one synchronous built-in message box owned by `window.dll`.
 *
 * The first version stays intentionally small: one text body, one implicit OK
 * action, and an optional error-notification visual treatment. `owner` is
 * reserved for future centering and modality policies and is currently ignored.
 *
 * @param params Caller-owned message-box description.
 * @return `ROS_MESSAGEBOX_RESULT_OK` when the user acknowledges the dialog, or
 * another non-positive value on failure or cancellation.
 */
long MessageBoxShow(const MessageBoxParams* params);

/*
 * Show one basic notification dialog with the default visual treatment.
 *
 * @param title Optional dialog title.
 * @param message Optional message body.
 * @return `ROS_MESSAGEBOX_RESULT_OK` when the user acknowledges the dialog, or
 * another non-positive value on failure or cancellation.
 */
long MessageBox(const char* title, const char* message);

/*
 * Show one error-notification dialog with the built-in error accent.
 *
 * @param title Optional dialog title.
 * @param message Optional message body.
 * @return `ROS_MESSAGEBOX_RESULT_OK` when the user acknowledges the dialog, or
 * another non-positive value on failure or cancellation.
 */
long MessageBoxError(const char* title, const char* message);

/*
 * Queue one background file read and invoke callbacks when the bytes are ready.
 *
 * The call returns immediately after `window.dll` enqueues the work on its
 * dedicated loader thread, so UI code can keep pumping messages while large
 * fonts, images, or other disk-backed assets are staged in the background.
 * Success and failure callbacks run on that background thread; callers that
 * need to mutate window state should post back to their owner thread.
 *
 * @param path DOS-style file path to read.
 * @param success_callback Callback invoked when the file bytes were loaded.
 * @param error_callback Callback invoked when the read fails.
 * @param context Opaque caller-owned cookie forwarded to the callback.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadFileAssetAsync(
    const char* path,
    WindowAssetLoadSuccessCallback success_callback,
    WindowAssetLoadErrorCallback error_callback,
    void* context);

/*
 * Queue one background file read and notify one UI thread with `PostMessage`.
 *
 * This Win32-style surface keeps disk I/O on window.dll's worker thread while
 * the owner thread remains the sole place that adopts buffers, updates window
 * state, and repaints the screen. The posted message carries the request id in
 * `wParam`; the UI thread then calls `ReceiveAssetCompletion` to claim the
 * result.
 *
 * @param path DOS-style file path to read.
 * @param hwnd Owner window that should receive the completion message.
 * @param message Window message identifier posted when the request completes.
 * @param context Opaque caller-owned cookie copied into the completion record.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadFileAssetAsyncNotify(const char* path, HWND hwnd, unsigned long message, void* context);

/*
 * Queue one background DLL-section read and invoke callbacks when the bytes are ready.
 *
 * Embedded assets are resolved by loading the target DLL on the background
 * worker, locating the named image section, copying that section into a shared-
 * heap buffer, and then unloading the DLL again. This keeps callers decoupled
 * from module lifetime and avoids borrowing mapped image memory after the load
 * finishes.
 *
 * @param module_path DOS-style DLL path that owns the embedded asset.
 * @param section_name Null-terminated image-section name, up to 8 characters.
 * @param success_callback Callback invoked when the section bytes were copied.
 * @param error_callback Callback invoked when the DLL or section lookup fails.
 * @param context Opaque caller-owned cookie forwarded to the callback.
 * @return Non-negative request id on success, or a negative status code on failure.
 */
long LoadModuleSectionAssetAsync(
    const char* module_path,
    const char* section_name,
    WindowAssetLoadSuccessCallback success_callback,
    WindowAssetLoadErrorCallback error_callback,
    void* context);

/*
 * Queue one background DLL-section read and notify one UI thread with `PostMessage`.
 *
 * This mirrors `LoadFileAssetAsyncNotify`, but the worker sources bytes from a
 * named image section inside another DLL before waking the owner thread.
 *
 * @param module_path DOS-style DLL path that owns the embedded asset.
 * @param section_name Null-terminated image-section name, up to 8 characters.
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
    void* context);

/*
 * Claim one queued async asset completion after its notification arrives.
 *
 * The completion queue is process-local to window.dll. `wParam` from the owner
 * thread's posted completion message identifies which result to remove.
 *
 * @param request_id Request identifier carried by the posted message.
 * @param completion Receives the copied completion record.
 * @return Zero on success, or a negative status code when the result is absent.
 */
long ReceiveAssetCompletion(unsigned long request_id, WindowAssetCompletion* completion);

/*
 * Release one asset buffer previously returned by a success callback.
 *
 * The background loader allocates returned bytes from the same shared user heap
 * used by the rest of the GUI stack, so consumers should release the buffer
 * through this helper rather than assuming ownership of the allocator.
 *
 * @param bytes Buffer previously returned by `LoadFileAssetAsync` or `LoadModuleSectionAssetAsync`.
 * @return Nothing.
 */
void FreeAssetBuffer(void* bytes);

#elif !defined(ROS_WINDOW_NO_IMPORTS) && !defined(ROS_WINDOW_SERVER_ONLY)

/*
 * Import the window-client helper surface from `window.dll` so EXEs keep their
 * Win32-style API while the stateful client queue lives in one dedicated DLL.
 */
DECLARE(long, CreateWindowClass, (const char* class_name, WNDPROC proc), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateWindowClass");
DECLARE(HWND, CreateWindowEx, (const WindowCreateParams* params), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateWindowEx");
DECLARE(HWND, CreateWindow, (const char* class_name, const char* title), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateWindow");
DECLARE(long, DestroyWindow, (HWND hwnd), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "DestroyWindow");
DECLARE(HMENU, CreateMenu, (void), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateMenu");
DECLARE(long, DestroyMenu, (HMENU menu), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "DestroyMenu");
DECLARE(long, AppendMenuItem, (HMENU menu, unsigned long command_id, unsigned long flags, const char* text, unsigned long hotkey), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "AppendMenuItem");
DECLARE(long, AppendSubMenu, (HMENU menu, HMENU submenu, unsigned long flags, const char* text, unsigned long hotkey), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "AppendSubMenu");
DECLARE(long, AppendMenuSeparator, (HMENU menu), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "AppendMenuSeparator");
DECLARE(long, TrackPopupMenu, (HMENU menu, unsigned long flags, long x, long y, HWND owner), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "TrackPopupMenu");
DECLARE(long, SetWindowCursor, (HWND hwnd, const char* cursor_path), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "SetWindowCursor");
DECLARE(long, MoveWindow, (HWND hwnd, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "MoveWindow");
DECLARE(long, SetForegroundWindow, (HWND hwnd), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "SetForegroundWindow");
DECLARE(long, PostSetForegroundWindow, (HWND hwnd), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "PostSetForegroundWindow");
DECLARE(unsigned long, SetTimer, (HWND hwnd, unsigned long timer_id, unsigned long interval_msec), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "SetTimer");
DECLARE(long, KillTimer, (HWND hwnd, unsigned long timer_id), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "KillTimer");
DECLARE(LRESULT, SendMessage, (HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "SendMessage");
DECLARE(long, PostMessage, (HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "PostMessage");
DECLARE(long, PostQuitMessage, (long exit_code), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "PostQuitMessage");
DECLARE(long, ReceiveProcessIpcMessage, (UserIpcMessage* packet, unsigned long flags), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "ReceiveProcessIpcMessage");
DECLARE(long, GetMessage, (MSG* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "GetMessage");
DECLARE(long, TranslateMessage, (const MSG* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "TranslateMessage");
DECLARE(LRESULT, DispatchMessage, (const MSG* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "DispatchMessage");
DECLARE(long, MessageBoxShow, (const MessageBoxParams* params), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "MessageBoxShow");
DECLARE(long, MessageBox, (const char* title, const char* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "MessageBox");
DECLARE(long, MessageBoxError, (const char* title, const char* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "MessageBoxError");
DECLARE(long, LoadFileAssetAsync, (const char* path, WindowAssetLoadSuccessCallback success_callback, WindowAssetLoadErrorCallback error_callback, void* context), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "LoadFileAssetAsync");
DECLARE(long, LoadFileAssetAsyncNotify, (const char* path, HWND hwnd, unsigned long message, void* context), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "LoadFileAssetAsyncNotify");
DECLARE(long, LoadModuleSectionAssetAsync, (const char* module_path, const char* section_name, WindowAssetLoadSuccessCallback success_callback, WindowAssetLoadErrorCallback error_callback, void* context), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "LoadModuleSectionAssetAsync");
DECLARE(long, LoadModuleSectionAssetAsyncNotify, (const char* module_path, const char* section_name, HWND hwnd, unsigned long message, void* context), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "LoadModuleSectionAssetAsyncNotify");
DECLARE(long, ReceiveAssetCompletion, (unsigned long request_id, WindowAssetCompletion* completion), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "ReceiveAssetCompletion");
DECLARE(void, FreeAssetBuffer, (void* bytes), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "FreeAssetBuffer");

/*
 * Return a stable printable name for one message identifier.
 *
 * @param message Message identifier.
 * @return Constant string describing the message.
 */
static inline const char* WindowMessageName(unsigned long message) {
    switch (message) {
    case WM_NULL:
        return "WM_NULL";
    case WM_CREATE:
        return "WM_CREATE";
    case WM_DESTROY:
        return "WM_DESTROY";
    case WM_REPAINT:
        return "WM_REPAINT";
    case WM_COMMAND:
        return "WM_COMMAND";
    case WM_RBUTTONDOWN:
        return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP:
        return "WM_RBUTTONUP";
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
        return message >= WM_USER ? "WM_USER" : "WM_UNKNOWN";
    }
}

#endif

#endif