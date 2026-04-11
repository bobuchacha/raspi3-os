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

#define ROS_WINDOW_CLASS_NAME_MAX ROS_USER_IPC_TEXT_MAX
#define ROS_WINDOW_TITLE_MAX ROS_USER_IPC_TEXT2_MAX
#define ROS_WINDOW_CURSOR_PATH_MAX ROS_USER_IPC_TEXT_MAX

#define ROS_WINDOW_CURSOR_DEFAULT_PATH "C:\\cursors\\arrow.cur32"
#define ROS_WINDOW_CURSOR_CROSSHAIR_PATH "C:\\cursors\\crosshair.cur32"
#define ROS_WINDOW_CURSOR_BUSY_PATH "C:\\cursors\\busy.cur32"

#define ROS_WINDOW_STYLE_VISIBLE (1UL << 0)
#define ROS_WINDOW_STYLE_CHILD (1UL << 1)
#define ROS_WINDOW_STYLE_DECORATED (1UL << 2)
#define ROS_WINDOW_STYLE_DISABLED (1UL << 3)
#define ROS_WINDOW_STYLE_BORDER (1UL << 4)

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
#define WM_USER 1024UL

typedef unsigned long HWND;
typedef long LRESULT;
typedef LRESULT(*WNDPROC)(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam);

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
 * Return a stable printable name for one message identifier.
 *
 * @param message Message identifier.
 * @return Constant string describing the message.
 */
const char* WindowMessageName(unsigned long message);

#else

/*
 * Import the window-client helper surface from `window.dll` so EXEs keep their
 * Win32-style API while the stateful client queue lives in one dedicated DLL.
 */
DECLARE(long, CreateWindowClass, (const char* class_name, WNDPROC proc), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateWindowClass");
DECLARE(HWND, CreateWindowEx, (const WindowCreateParams* params), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateWindowEx");
DECLARE(HWND, CreateWindow, (const char* class_name, const char* title), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "CreateWindow");
DECLARE(long, SetWindowCursor, (HWND hwnd, const char* cursor_path), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "SetWindowCursor");
DECLARE(long, MoveWindow, (HWND hwnd, long x, long y, unsigned long width, unsigned long height), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "MoveWindow");
DECLARE(LRESULT, SendMessage, (HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "SendMessage");
DECLARE(long, PostMessage, (HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "PostMessage");
DECLARE(long, PostQuitMessage, (long exit_code), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "PostQuitMessage");
DECLARE(long, GetMessage, (MSG* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "GetMessage");
DECLARE(long, TranslateMessage, (const MSG* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "TranslateMessage");
DECLARE(LRESULT, DispatchMessage, (const MSG* message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "DispatchMessage");
DECLARE(const char*, WindowMessageName, (unsigned long message), FROM, ROS_WINDOW_CLIENT_MODULE_NAME, "WindowMessageName");

#endif

#endif