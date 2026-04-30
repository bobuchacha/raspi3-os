#define ROS_WINDOW_SERVER_ONLY 1
#define ROS_WINDOW_NO_IMPORTS 1
#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_EXPLORER_SHELL 1
#include "app/app.h"
#include "gwes_workers.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GWES_INPUT_BATCH_LIMIT 8UL
#define GWES_DIALOG_BORDER_THICKNESS 4UL
#define GWES_DIALOG_TITLE_HEIGHT 28UL
#define GWES_DIALOG_CLOSE_SIZE 18UL
#define GWES_DIALOG_CLOSE_MARGIN 5UL
#define GWES_DIALOG_CONTROL_BUTTON_GAP 4UL
#define GWES_DIALOG_RESIZE_GRIP 6UL
#define GWES_DECORATED_MIN_WIDTH ((GWES_DIALOG_BORDER_THICKNESS * 2UL) + 96UL)
#define GWES_DECORATED_MIN_HEIGHT (GWES_DIALOG_TITLE_HEIGHT + GWES_DIALOG_BORDER_THICKNESS + 48UL)
#define GWES_EXPLORER_DESKTOP_CLASS "explorer.desktop"

typedef enum GwesWindowShowState {
    GWES_WINDOW_SHOW_NORMAL = 0UL,
    GWES_WINDOW_SHOW_MINIMIZED = 1UL,
    GWES_WINDOW_SHOW_MAXIMIZED = 2UL
} GwesWindowShowState;

typedef struct GwesClassRecord {
    int in_use;
    long owner_pid;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    struct GwesClassRecord* next;
} GwesClassRecord;

typedef struct GwesWindowRecord {
    int in_use;
    long owner_pid;
    HWND hwnd;
    HWND parent;
    long x;
    long y;
    unsigned long width;
    unsigned long height;
    unsigned long style;
    char class_name[ROS_WINDOW_CLASS_NAME_MAX];
    char title[ROS_WINDOW_TITLE_MAX];
    char cursor_path[ROS_WINDOW_CURSOR_PATH_MAX];
    unsigned long show_state;
    unsigned long minimized_restore_state;
    int restore_valid;
    long restore_x;
    long restore_y;
    unsigned long restore_width;
    unsigned long restore_height;
    struct GwesWindowRecord* next;
} GwesWindowRecord;

typedef struct GwesCleanupSummary {
    unsigned long released_classes;
    unsigned long released_windows;
} GwesCleanupSummary;

typedef struct GwesTimerRecord {
    int in_use;
    long owner_pid;
    HWND hwnd;
    unsigned long timer_id;
    unsigned long interval_msec;
    unsigned long next_fire_msec;
    struct GwesTimerRecord* next;
} GwesTimerRecord;

static GwesClassRecord* g_gwes_classes = NULL;
static GwesWindowRecord* g_gwes_windows = NULL;
static GwesTimerRecord* g_gwes_timers = NULL;
static unsigned long g_gwes_class_count = 0UL;
static unsigned long g_gwes_class_peak = 0UL;
static unsigned long g_gwes_window_count = 0UL;
static unsigned long g_gwes_window_peak = 0UL;
static HWND g_gwes_next_hwnd = 1UL;
static unsigned long g_gwes_next_timer_id = 1UL;
static RosKernelGuiSharedInputRegion* g_gwes_shared_input = NULL;
static unsigned long g_gwes_shared_input_consumer_index = 0UL;
static int g_gwes_shared_input_attached = 0;
static RosExplorerShellSharedState* g_gwes_shell_state = NULL;
static HWND g_gwes_focus_hwnd = 0UL;
static HWND g_gwes_last_task_focus_hwnd = 0UL;
static HWND g_gwes_keyboard_capture_hwnd = 0UL;
static HWND g_gwes_pointer_capture_hwnd = 0UL;
static HWND g_gwes_pointer_down_hwnd = 0UL;
static HWND g_gwes_hover_hwnd = 0UL;
static HWND g_gwes_pointer_interaction_hwnd = 0UL;
static unsigned long g_gwes_pointer_resize_edges = 0UL;
static unsigned long g_gwes_pointer_x = 0UL;
static unsigned long g_gwes_pointer_y = 0UL;
static unsigned long g_gwes_pointer_drag_start_x = 0UL;
static unsigned long g_gwes_pointer_drag_start_y = 0UL;
static long g_gwes_pointer_drag_origin_x = 0L;
static long g_gwes_pointer_drag_origin_y = 0L;
static unsigned long g_gwes_pointer_drag_origin_width = 0UL;
static unsigned long g_gwes_pointer_drag_origin_height = 0UL;
static long g_gwes_pointer_drag_offset_x = 0L;
static long g_gwes_pointer_drag_offset_y = 0L;
static int g_gwes_pointer_preview_active = 0;
static long g_gwes_pointer_preview_x = 0L;
static long g_gwes_pointer_preview_y = 0L;
static unsigned long g_gwes_pointer_preview_width = 0UL;
static unsigned long g_gwes_pointer_preview_height = 0UL;
static int g_gwes_pointer_visible = 0;

static void gwes_unpack_pair(unsigned long packed, unsigned long* first, unsigned long* second);
static void gwes_unpack_signed_pair(unsigned long packed, long* first, long* second);
static void gwes_send_window_message(GwesWindowRecord* window, unsigned long message, unsigned long wparam, unsigned long lparam);
static GwesWindowRecord* gwes_resolve_pointer_target(unsigned long x, unsigned long y, long* local_x, long* local_y);
static const char* gwes_effective_cursor_path(GwesWindowRecord* window);
static void gwes_refresh_pointer_cursor(void);
static void gwes_refresh_pointer_cursor_for_target(GwesWindowRecord* target);
static void gwes_sync_pointer_snapshot(const RosKernelGuiPointerState* pointer_state);
static void gwes_log_line(const char* text);
static void gwes_log_registry_counts(const char* reason);
static void gwes_forget_window_state(unsigned long hwnd);
static void gwes_release_window_timers(unsigned long hwnd);
static void gwes_publish_shell_state(void);
static void gwes_send_shell_ipc(unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3);
static int g_gwes_desktop_pointer_down = 0;

/*
 * Return whether GWES should post one message asynchronously rather than
 * forcing immediate in-thread handling by the client.
 *
 * This follows the Win32 / WinCE split the current userspace surface models:
 * high-frequency input and paint-style notifications should be posted so the
 * app thread can coalesce them, while synchronous `SendMessage` remains a
 * client-local API for direct state mutation and return values.
 *
 * @param message Window message identifier.
 * @return Non-zero when the message belongs on the async queue.
 */
static int gwes_message_prefers_post(unsigned long message) {
    (void)message;
    return 1;
}

/*
 * Pack two signed 32-bit coordinates into one window-message payload.
 *
 * @param first Upper signed payload.
 * @param second Lower signed payload.
 * @return Packed message parameter.
 */
static unsigned long gwes_pack_signed_pair(long first, long second) {
    return WindowPackSignedPair(first, second);
}

/*
 * Return the larger of two unsigned values without relying on hosted libc.
 *
 * @param first First candidate value.
 * @param second Second candidate value.
 * @return Larger input value.
 */
static unsigned long gwes_max_ul(unsigned long first, unsigned long second) {
    return first > second ? first : second;
}

/*
 * Pack two unsigned 32-bit values into one message payload.
 *
 * @param first Upper value.
 * @param second Lower value.
 * @return Packed payload word.
 */
static unsigned long gwes_pack_unsigned_pair(unsigned long first, unsigned long second) {
    return (((first & 0xFFFFFFFFUL) << 32) | (second & 0xFFFFFFFFUL));
}

/*
 * Cache and draw one interactive move/resize preview rectangle.
 *
 * GWES uses this to emulate classic Win9x placeholder dragging so the live
 * window geometry remains stable until the interaction commits on button-up.
 * That keeps clients out of the hot path for every pointer sample.
 *
 * @param x Preview outer-frame X coordinate.
 * @param y Preview outer-frame Y coordinate.
 * @param width Preview outer-frame width.
 * @param height Preview outer-frame height.
 * @return Nothing.
 */
static void gwes_set_pointer_interaction_preview(long x, long y, unsigned long width, unsigned long height) {
    if (x < 0L) {
        x = 0L;
    }
    if (y < 0L) {
        y = 0L;
    }

    if (g_gwes_pointer_preview_active
        && g_gwes_pointer_preview_x == x
        && g_gwes_pointer_preview_y == y
        && g_gwes_pointer_preview_width == width
        && g_gwes_pointer_preview_height == height) {
        return;
    }

    g_gwes_pointer_preview_active = 1;
    g_gwes_pointer_preview_x = x;
    g_gwes_pointer_preview_y = y;
    g_gwes_pointer_preview_width = width;
    g_gwes_pointer_preview_height = height;
    gwes_render_set_interaction_placeholder(x, y, width, height);
}

/*
 * Order one load barrier before consuming records from the shared-input ring.
 *
 * @return Nothing.
 */
static void gwes_shared_input_barrier(void) {
    asm volatile("dmb ishld" ::: "memory");
}

/*
 * Copy one string into a fixed-size destination and always terminate it.
 *
 * @param destination Output buffer.
 * @param size Output buffer size.
 * @param text Source string.
 * @return Nothing.
 */
static void gwes_copy_text(char* destination, unsigned long size, const char* text) {
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
 * Write one tagged GWES log line to the serial console.
 *
 * @param text Message body to print.
 * @return Nothing.
 */
static void gwes_log_line(const char* text) {
    char line[192];

    snprintf(line, sizeof(line), "gwes.exe: %s", text ? text : "");
    writeLine(line);
}

/*
 * Write one numeric GWES log line.
 *
 * @param prefix Line prefix.
 * @param value Numeric value appended after the prefix.
 * @return Nothing.
 */
static void gwes_log_value(const char* prefix, unsigned long value) {
    char line[192];

    snprintf(line, sizeof(line), "gwes.exe: %s%lu", prefix ? prefix : "", value);
    writeLine(line);
}

/*
 * Write one structured create-window failure line so client logs can be
 * correlated with the exact server-side rejection point.
 *
 * @param stage Short failure stage label.
 * @param packet Original client packet when available.
 * @param status Numeric failure code returned to the client.
 * @return Nothing.
 */
static void gwes_log_create_failure(const char* stage, const UserIpcMessage* packet, long status) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: create failed stage=%s pid=%ld class=%s title=%s style=%lu size=%lux%lu status=%ld",
        stage ? stage : "unknown",
        packet ? packet->sender_pid : -1L,
        (packet && packet->text[0] != '\0') ? packet->text : "<null>",
        (packet && packet->text2[0] != '\0') ? packet->text2 : "",
        packet ? packet->arg3 : 0UL,
        packet ? ((packet->arg2 >> 32) & 0xFFFFFFFFUL) : 0UL,
        packet ? (packet->arg2 & 0xFFFFFFFFUL) : 0UL,
        status);
    writeLine(line);
}

/*
 * Write one compact client-request line for the low-frequency register/create
 * handshake paths.
 *
 * @param stage Short request label.
 * @param packet Original client packet.
 * @return Nothing.
 */
static void gwes_log_client_request(const char* stage, const UserIpcMessage* packet) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: request stage=%s pid=%ld kind=%lu class=%s title=%s",
        stage ? stage : "unknown",
        packet ? packet->sender_pid : -1L,
        packet ? packet->kind : 0UL,
        (packet && packet->text[0] != '\0') ? packet->text : "<null>",
        (packet && packet->text2[0] != '\0') ? packet->text2 : "");
    writeLine(line);
}

/*
 * Write one structured cleanup line that explains why GWES released one
 * client's server-side objects.
 *
 * @param owner_pid Client process identifier being cleaned up.
 * @param exit_code Application-provided quit code when available.
 * @param reason Short reason string for the cleanup path.
 * @param summary Counts of released objects.
 * @return Nothing.
 */
static void gwes_log_cleanup(long owner_pid, unsigned long exit_code, const char* reason, GwesCleanupSummary summary) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: cleanup pid=%ld exit=%lu reason=%s classes=%lu windows=%lu",
        owner_pid,
        exit_code,
        reason ? reason : "unknown",
        summary.released_classes,
        summary.released_windows);
    writeLine(line);
}

/*
 * Emit one compact registry occupancy line for scaling diagnostics.
 *
 * These counters let large multi-process GUI tests show whether the next
 * pressure point is class growth, window growth, or something lower in the
 * kernel. GWES owns relatively few objects, so logging on create/release is
 * cheap and far more actionable than a silent `NO_SPACE` path.
 *
 * @param reason Short label describing why the snapshot is being emitted.
 * @return Nothing.
 */
static void gwes_log_registry_counts(const char* reason) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: registry reason=%s classes=%lu peak=%lu windows=%lu peak=%lu",
        reason ? reason : "unknown",
        g_gwes_class_count,
        g_gwes_class_peak,
        g_gwes_window_count,
        g_gwes_window_peak);
    writeLine(line);
}

/*
 * Record one class-count increase and emit a snapshot when the high-water mark
 * changes.
 *
 * @return Nothing.
 */
static void gwes_note_class_alloc(void) {
    ++g_gwes_class_count;
    if (g_gwes_class_count > g_gwes_class_peak) {
        g_gwes_class_peak = g_gwes_class_count;
    }

    gwes_log_registry_counts("class-alloc");
}

/*
 * Record one class-count decrease and emit a snapshot for live occupancy.
 *
 * @return Nothing.
 */
static void gwes_note_class_release(void) {
    if (g_gwes_class_count != 0UL) {
        --g_gwes_class_count;
    }

    gwes_log_registry_counts("class-release");
}

/*
 * Record one window-count increase and emit a snapshot when the high-water
 * mark changes.
 *
 * @return Nothing.
 */
static void gwes_note_window_alloc(void) {
    ++g_gwes_window_count;
    if (g_gwes_window_count > g_gwes_window_peak) {
        g_gwes_window_peak = g_gwes_window_count;
    }

    gwes_log_registry_counts("window-alloc");
}

/*
 * Record one window-count decrease and emit a snapshot for live occupancy.
 *
 * @return Nothing.
 */
static void gwes_note_window_release(void) {
    if (g_gwes_window_count != 0UL) {
        --g_gwes_window_count;
    }

    gwes_log_registry_counts("window-release");
}

/*
 * Report whether one PID still resolves to a live task.
 *
 * @param pid Process identifier to query.
 * @return Non-zero when the PID is live.
 */
static int gwes_pid_is_live(long pid) {
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
 * Send one explorer shell-control IPC packet to the published shell process.
 *
 * @param kind Explorer shell packet kind.
 * @param arg0 First scalar payload.
 * @param arg1 Second scalar payload.
 * @param arg2 Third scalar payload.
 * @param arg3 Fourth scalar payload.
 * @return Nothing.
 */
static void gwes_send_shell_ipc(unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3) {
    UserIpcMessage packet;
    long shell_pid;

    if (g_gwes_shell_state == NULL || g_gwes_shell_state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION) {
        return;
    }

    shell_pid = (long)g_gwes_shell_state->shell_pid;
    if (!gwes_pid_is_live(shell_pid)) {
        return;
    }

    memset(&packet, 0, sizeof(packet));
    packet.protocol = ROS_EXPLORER_SHELL_IPC_PROTOCOL;
    packet.kind = kind;
    packet.arg0 = arg0;
    packet.arg1 = arg1;
    packet.arg2 = arg2;
    packet.arg3 = arg3;
    {
        char msg[192];
        snprintf(msg, sizeof(msg), "send_shell_ipc pid=%ld kind=%lu arg0=%lu arg1=%lu arg2=%lu", shell_pid, kind, arg0, arg1, arg2);
        gwes_log_line(msg);
        long send_status = sendUserIpcMessage(shell_pid, &packet);
        if (send_status < 0L) {
            char err[192];
            snprintf(err, sizeof(err), "send_shell_ipc failed pid=%ld status=%ld", shell_pid, send_status);
            gwes_log_line(err);
        }
    }
}

/*
 * Find one previously registered client class.
 *
 * @param owner_pid Owning client PID.
 * @param class_name Registered class name.
 * @return Matching class record, or NULL when none exists.
 */
static GwesClassRecord* gwes_find_class(long owner_pid, const char* class_name) {
    GwesClassRecord* record;

    for (record = g_gwes_classes; record != NULL; record = record->next) {
        if (!record->in_use) {
            continue;
        }
        if (record->owner_pid != owner_pid) {
            continue;
        }
        if (userIpcTextEquals(record->class_name, class_name)) {
            return record;
        }
    }

    return NULL;
}

/*
 * Allocate one class record on the process heap and link it into GWES.
 *
 * Heap-backed records remove the previous hard stop at a compile-time class
 * table size and let scaling follow available memory instead.
 *
 * @return New class record, or NULL when allocation fails.
 */
static GwesClassRecord* gwes_allocate_class(void) {
    GwesClassRecord* record = (GwesClassRecord*)malloc(sizeof(*record));

    if (record == NULL) {
        gwes_log_registry_counts("class-alloc-failed");
        return NULL;
    }

    memset(record, 0, sizeof(*record));
    record->next = g_gwes_classes;
    g_gwes_classes = record;
    gwes_note_class_alloc();
    return record;
}

/*
 * Release one class record and unlink it from the GWES registry.
 *
 * @param record Class record to unlink.
 * @return Nothing.
 */
static void gwes_release_class_record(GwesClassRecord* record) {
    GwesClassRecord** link;

    if (record == NULL) {
        return;
    }

    for (link = &g_gwes_classes; *link != NULL; link = &((*link)->next)) {
        if (*link == record) {
            *link = record->next;
            free(record);
            gwes_note_class_release();
            return;
        }
    }
}

/*
 * Allocate one server window record on the process heap and link it into the
 * live registry immediately.
 *
 * The record must exist before client create processing continues because the
 * Win32-style window lifecycle can synchronously feed messages back into the
 * framework during creation.
 *
 * @return New window record, or NULL when allocation fails.
 */
static GwesWindowRecord* gwes_allocate_window(void) {
    GwesWindowRecord* window = (GwesWindowRecord*)malloc(sizeof(*window));

    if (window == NULL) {
        gwes_log_registry_counts("window-alloc-failed");
        return NULL;
    }

    memset(window, 0, sizeof(*window));
    window->next = g_gwes_windows;
    g_gwes_windows = window;
    gwes_note_window_alloc();
    return window;
}

/*
 * Release one server window record and optionally tear down its render state.
 *
 * @param window Window record to unlink.
 * @param destroy_render_state Non-zero when compositor resources exist.
 * @return Nothing.
 */
static void gwes_release_window_record(GwesWindowRecord* window, int destroy_render_state) {
    GwesWindowRecord** link;

    if (window == NULL) {
        return;
    }

    for (link = &g_gwes_windows; *link != NULL; link = &((*link)->next)) {
        if (*link == window) {
            *link = window->next;
            gwes_release_window_timers(window->hwnd);
            if (destroy_render_state) {
                gwes_forget_window_state(window->hwnd);
                gwes_render_destroy_window(window->hwnd);
            }
            free(window);
            gwes_note_window_release();
            gwes_publish_shell_state();
            return;
        }
    }

}

/*
 * Find one live timer owned by the specified client and window.
 *
 * @param owner_pid Client process identifier.
 * @param hwnd Target window handle.
 * @param timer_id Timer identifier.
 * @return Matching timer record, or NULL when none exists.
 */
static GwesTimerRecord* gwes_find_timer(long owner_pid, unsigned long hwnd, unsigned long timer_id) {
    GwesTimerRecord* timer;

    for (timer = g_gwes_timers; timer != NULL; timer = timer->next) {
        if (!timer->in_use) {
            continue;
        }
        if (timer->owner_pid != owner_pid || timer->hwnd != hwnd || timer->timer_id != timer_id) {
            continue;
        }
        return timer;
    }

    return NULL;
}

/*
 * Allocate one heap-backed timer record and link it into the live registry.
 *
 * @return New timer record, or NULL when allocation fails.
 */
static GwesTimerRecord* gwes_allocate_timer(void) {
    GwesTimerRecord* timer = (GwesTimerRecord*)malloc(sizeof(*timer));

    if (timer == NULL) {
        return NULL;
    }

    memset(timer, 0, sizeof(*timer));
    timer->next = g_gwes_timers;
    g_gwes_timers = timer;
    return timer;
}

/*
 * Release one timer record and unlink it from the live registry.
 *
 * @param timer Timer record to release.
 * @return Nothing.
 */
static void gwes_release_timer_record(GwesTimerRecord* timer) {
    GwesTimerRecord** link;

    if (timer == NULL) {
        return;
    }

    for (link = &g_gwes_timers; *link != NULL; link = &((*link)->next)) {
        if (*link == timer) {
            *link = timer->next;
            free(timer);
            return;
        }
    }
}

/*
 * Release every timer currently owned by one window.
 *
 * @param hwnd Window whose timers should be removed.
 * @return Nothing.
 */
static void gwes_release_window_timers(unsigned long hwnd) {
    GwesTimerRecord* timer = g_gwes_timers;

    while (timer != NULL) {
        GwesTimerRecord* next = timer->next;

        if (timer->hwnd == hwnd) {
            gwes_release_timer_record(timer);
        }

        timer = next;
    }
}

/*
 * Find one live server window by its owner and handle.
 *
 * @param owner_pid Client process identifier that owns the window.
 * @param hwnd Stable window identifier assigned by GWES.
 * @return Matching window record, or NULL when no owned window exists.
 */
static GwesWindowRecord* gwes_find_window(long owner_pid, unsigned long hwnd) {
    GwesWindowRecord* window;

    for (window = g_gwes_windows; window != NULL; window = window->next) {
        if (!window->in_use) {
            continue;
        }
        if (window->owner_pid != owner_pid) {
            continue;
        }
        if (window->hwnd == hwnd) {
            return window;
        }
    }

    return NULL;
}

/*
 * Find one live server window by handle regardless of owner PID.
 *
 * @param hwnd Stable window identifier assigned by GWES.
 * @return Matching window record, or NULL when no window exists.
 */
static GwesWindowRecord* gwes_find_window_any(unsigned long hwnd) {
    GwesWindowRecord* window;

    for (window = g_gwes_windows; window != NULL; window = window->next) {
        if (window->in_use && window->hwnd == hwnd) {
            return window;
        }
    }

    return NULL;
}

/*
 * Follow parent pointers until one top-level window is reached.
 *
 * @param window Starting child or top-level window.
 * @return Top-level ancestor, or the original window when it has no parent.
 */
static GwesWindowRecord* gwes_find_root_window(GwesWindowRecord* window) {
    GwesWindowRecord* current = window;

    while (current != NULL && current->parent != 0UL) {
        current = gwes_find_window_any(current->parent);
    }

    return current;
}

/*
 * Drop focus, capture, and press bookkeeping when a window disappears.
 *
 * @param hwnd Window handle being released.
 * @return Nothing.
 */
static void gwes_forget_window_state(unsigned long hwnd) {
    if (g_gwes_focus_hwnd == hwnd) {
        g_gwes_focus_hwnd = 0UL;
    }
    if (g_gwes_last_task_focus_hwnd == hwnd) {
        g_gwes_last_task_focus_hwnd = 0UL;
    }
    if (g_gwes_keyboard_capture_hwnd == hwnd) {
        g_gwes_keyboard_capture_hwnd = 0UL;
    }
    if (g_gwes_pointer_capture_hwnd == hwnd) {
        g_gwes_pointer_capture_hwnd = 0UL;
    }
    if (g_gwes_pointer_down_hwnd == hwnd) {
        g_gwes_pointer_down_hwnd = 0UL;
    }
    if (g_gwes_hover_hwnd == hwnd) {
        g_gwes_hover_hwnd = 0UL;
    }
    if (g_gwes_pointer_interaction_hwnd == hwnd) {
        g_gwes_pointer_interaction_hwnd = 0UL;
        g_gwes_pointer_capture_hwnd = 0UL;
        g_gwes_pointer_resize_edges = 0UL;
    }
}

/*
 * Record one new keyboard focus target when the handle is still live.
 *
 * @param hwnd Focus target.
 * @return Nothing.
 */
static void gwes_set_focus(unsigned long hwnd) {
    GwesWindowRecord* focus_window = gwes_find_window_any(hwnd);
    GwesWindowRecord* root = gwes_find_root_window(focus_window);

    g_gwes_focus_hwnd = focus_window != NULL ? hwnd : 0UL;
    if (root != NULL
        && root->parent == 0UL
        && (root->style & ROS_WINDOW_STYLE_VISIBLE) != 0UL
        && (root->style & ROS_WINDOW_STYLE_SYSTEM_UI) == 0UL) {
        g_gwes_last_task_focus_hwnd = root->hwnd;
    }
    gwes_publish_shell_state();
}

/*
 * Map the shared shell-state block used by GWES and explorer.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_shell_state_acquire(void) {
    if (g_gwes_shell_state != NULL) {
        return ROS_USER_IPC_STATUS_OK;
    }

    g_gwes_shell_state = ExplorerShellSharedState(sizeof(RosExplorerShellSharedState));
    if (g_gwes_shell_state == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (g_gwes_shell_state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION
        || g_gwes_shell_state->task_capacity != ROS_EXPLORER_SHELL_TASK_MAX) {
        memset(g_gwes_shell_state, 0, sizeof(*g_gwes_shell_state));
        ExplorerShellInitializeSharedState(g_gwes_shell_state);
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Report whether one top-level window should appear on the explorer taskbar.
 *
 * @param window Candidate server window.
 * @return Non-zero when the window should be published to explorer.
 */
static int gwes_window_is_task_candidate(const GwesWindowRecord* window) {
    return window != NULL
        && window->in_use
        && window->parent == 0UL
        && (window->style & ROS_WINDOW_STYLE_VISIBLE) != 0UL
        && (window->style & ROS_WINDOW_STYLE_SYSTEM_UI) == 0UL;
}

/*
 * Resolve the top-level task window that explorer should currently highlight.
 *
 * System UI windows can take focus while a menu is open, so the taskbar keeps
 * publishing the last non-shell top-level target in that case.
 *
 * @return Foreground top-level task window handle, or zero when none exists.
 */
static unsigned long gwes_task_foreground_hwnd(void) {
    GwesWindowRecord* focused = gwes_find_window_any(g_gwes_focus_hwnd);
    GwesWindowRecord* focused_root = gwes_find_root_window(focused);

    if (gwes_window_is_task_candidate(focused_root)) {
        return focused_root->hwnd;
    }

    focused = gwes_find_window_any(g_gwes_last_task_focus_hwnd);
    focused_root = gwes_find_root_window(focused);
    return gwes_window_is_task_candidate(focused_root) ? focused_root->hwnd : 0UL;
}

/*
 * Resolve the currently focused top-level window, including shell chrome.
 *
 * Explorer needs the real shell-focus target to keep the start menu open while
 * the menu itself owns focus. This is separate from the published taskbar
 * highlight target, which intentionally stays pinned to the last non-shell app.
 *
 * @return Focused top-level window handle, or zero when focus is unset.
 */
static unsigned long gwes_focused_root_hwnd(void) {
    GwesWindowRecord* focused = gwes_find_window_any(g_gwes_focus_hwnd);
    GwesWindowRecord* focused_root = gwes_find_root_window(focused);

    if (focused_root != NULL) {
        return focused_root->hwnd;
    }

    return focused != NULL ? focused->hwnd : 0UL;
}

/*
 * Return whether one published task window currently requests fullscreen shell behavior.
 *
 * @param window Candidate top-level window.
 * @return Non-zero when the fullscreen style bit is set.
 */
static int gwes_window_requests_fullscreen(const GwesWindowRecord* window) {
    return gwes_window_is_task_candidate(window)
        && (window->style & ROS_WINDOW_STYLE_FULLSCREEN) != 0UL;
}

/*
 * Query the current desktop work area after shell reservations are applied.
 *
 * @param x Receives the work-area origin X.
 * @param y Receives the work-area origin Y.
 * @param width Receives the usable desktop width.
 * @param height Receives the usable desktop height.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_query_shell_work_area(long* x, long* y, unsigned long* width, unsigned long* height) {
    unsigned long desktop_width = 0UL;
    unsigned long desktop_height = 0UL;

    if (x == NULL || y == NULL || width == NULL || height == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (gwes_render_query_desktop_size(&desktop_width, &desktop_height) < 0L) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    *x = 0L;
    *y = 0L;
    *width = desktop_width;
    *height = desktop_height;

    if (g_gwes_shell_state == NULL || g_gwes_shell_state->version != ROS_EXPLORER_SHELL_SHARED_STATE_VERSION) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (g_gwes_shell_state->fullscreen_hwnd != 0ULL) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (g_gwes_shell_state->taskbar_visible == 0U || g_gwes_shell_state->taskbar_height == 0U) {
        return ROS_USER_IPC_STATUS_OK;
    }
    if (desktop_height <= g_gwes_shell_state->taskbar_height) {
        *height = 0UL;
        return ROS_USER_IPC_STATUS_OK;
    }

    *height = desktop_height - g_gwes_shell_state->taskbar_height;
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Publish one fresh task snapshot and work-area view into the shared shell state.
 *
 * @return Nothing.
 */
static void gwes_publish_shell_state(void) {
    RosExplorerShellSharedState* state;
    unsigned long desktop_width = 0UL;
    unsigned long desktop_height = 0UL;
    unsigned long active_hwnd;
    GwesWindowRecord* active_window;
    unsigned long task_count = 0UL;
    unsigned long index;

    if (gwes_shell_state_acquire() < 0L) {
        return;
    }

    state = g_gwes_shell_state;
    if (state == NULL) {
        return;
    }

    if (gwes_render_query_desktop_size(&desktop_width, &desktop_height) < 0L) {
        desktop_width = 0UL;
        desktop_height = 0UL;
    }

    active_hwnd = gwes_task_foreground_hwnd();
    active_window = gwes_find_window_any(active_hwnd);
    state->foreground_hwnd = active_hwnd;
    state->focused_hwnd = gwes_focused_root_hwnd();
    state->fullscreen_hwnd = gwes_window_requests_fullscreen(active_window) ? active_hwnd : 0ULL;
    state->desktop_width = desktop_width;
    state->desktop_height = desktop_height;
    state->work_area_x = 0L;
    state->work_area_y = 0L;
    state->work_area_width = desktop_width;
    state->work_area_height = desktop_height;
    if (state->fullscreen_hwnd == 0ULL
        && state->taskbar_visible != 0U
        && state->taskbar_height != 0U
        && desktop_height > state->taskbar_height) {
        state->work_area_height = desktop_height - state->taskbar_height;
    }

    for (index = 0UL; index < ROS_EXPLORER_SHELL_TASK_MAX; ++index) {
        memset(&state->tasks[index], 0, sizeof(state->tasks[index]));
    }

    for (GwesWindowRecord* window = g_gwes_windows; window != NULL && task_count < ROS_EXPLORER_SHELL_TASK_MAX; window = window->next) {
        RosExplorerShellTaskEntry* entry;

        if (!gwes_window_is_task_candidate(window)) {
            continue;
        }

        entry = &state->tasks[task_count++];
        entry->hwnd = window->hwnd;
        entry->owner_pid = window->owner_pid;
        entry->x = window->x;
        entry->y = window->y;
        entry->width = window->width;
        entry->height = window->height;
        entry->style = window->style;
        entry->flags = ROS_EXPLORER_SHELL_TASK_FLAG_VISIBLE;
        if (window->hwnd == active_hwnd) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_ACTIVE;
        }
        if ((window->style & ROS_WINDOW_STYLE_DECORATED) != 0UL) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_DECORATED;
        }
        if ((window->style & ROS_WINDOW_STYLE_TOPMOST) != 0UL) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_TOPMOST;
        }
        if ((window->style & ROS_WINDOW_STYLE_FULLSCREEN) != 0UL) {
            entry->flags |= ROS_EXPLORER_SHELL_TASK_FLAG_FULLSCREEN;
        }
        gwes_copy_text(entry->class_name, sizeof(entry->class_name), window->class_name);
        gwes_copy_text(entry->title, sizeof(entry->title), window->title);
    }

    state->task_count = task_count;
    ++state->task_generation;
}

/*
 * Report whether one server window is a decorated top-level dialog.
 *
 * @param window Candidate server window.
 * @return Non-zero when the window owns GWES-managed chrome.
 */
static int gwes_window_is_decorated_root(const GwesWindowRecord* window) {
    return window != NULL && window->parent == 0UL && (window->style & ROS_WINDOW_STYLE_DECORATED) != 0UL;
}

/*
 * Report whether one server window is Explorer's painted desktop background.
 *
 * Explorer currently uses a full-screen root window to paint the wallpaper and
 * forward shell-only button gestures, but routing every background mouse move
 * through that window makes empty-desktop motion noticeably heavier than motion
 * over normal application windows. GWES therefore recognizes the background
 * explicitly so pointer-move dispatch can take the cheaper no-target path while
 * button-down and button-up messages still reach Explorer for menu dismissal.
 *
 * @param window Candidate server window.
 * @return Non-zero when the window matches the non-topmost Explorer desktop.
 */
static int gwes_window_is_desktop_background(const GwesWindowRecord* window) {
    return window != NULL
        && window->parent == 0UL
        && (window->style & ROS_WINDOW_STYLE_SYSTEM_UI) != 0UL
        && (window->style & ROS_WINDOW_STYLE_TOPMOST) == 0UL
        && (window->style & ROS_WINDOW_STYLE_DECORATED) == 0UL
        && userIpcTextEquals(window->class_name, GWES_EXPLORER_DESKTOP_CLASS);
}

/*
 * Filter one pointer-move target before GWES updates hover or posts messages.
 *
 * Empty desktop movement should not wake Explorer's desktop window on every
 * sample. Returning null here preserves the fast path already used when the
 * pointer is over no interactive window at all, which is exactly the behavior
 * the desktop background wants for motion.
 *
 * @param target Hit-tested pointer target.
 * @return Original target, or null when the desktop background should be idle.
 */
static GwesWindowRecord* gwes_filter_pointer_move_target(GwesWindowRecord* target) {
    if (gwes_window_is_desktop_background(target)) {
        return NULL;
    }

    return target;
}

/*
 * Snapshot one decorated top-level window's restorable geometry.
 *
 * @param window Target window record.
 * @return Nothing.
 */
static void gwes_store_restore_geometry(GwesWindowRecord* window) {
    if (!gwes_window_is_decorated_root(window)) {
        return;
    }

    window->restore_valid = 1;
    window->restore_x = window->x;
    window->restore_y = window->y;
    window->restore_width = window->width;
    window->restore_height = window->height;
}

/*
 * Compute one dialog-client width for Win32-style `WM_SIZE` delivery.
 *
 * @param window Window whose client width is needed.
 * @return Client width in pixels.
 */
static unsigned long gwes_window_client_width(const GwesWindowRecord* window) {
    if (!gwes_window_is_decorated_root(window)) {
        return window != NULL ? window->width : 0UL;
    }

    return window->width > (GWES_DIALOG_BORDER_THICKNESS * 2UL)
        ? (window->width - (GWES_DIALOG_BORDER_THICKNESS * 2UL))
        : 1UL;
}

/*
 * Compute one dialog-client height for Win32-style `WM_SIZE` delivery.
 *
 * @param window Window whose client height is needed.
 * @return Client height in pixels.
 */
static unsigned long gwes_window_client_height(const GwesWindowRecord* window) {
    if (!gwes_window_is_decorated_root(window)) {
        return window != NULL ? window->height : 0UL;
    }

    return window->height > (GWES_DIALOG_TITLE_HEIGHT + GWES_DIALOG_BORDER_THICKNESS)
        ? (window->height - GWES_DIALOG_TITLE_HEIGHT - GWES_DIALOG_BORDER_THICKNESS)
        : 1UL;
}

/*
 * Send one `WM_MOVE` notification using client-origin coordinates.
 *
 * @param window Target window that moved.
 * @return Nothing.
 */
static void gwes_send_move_message(GwesWindowRecord* window) {
    long move_x;
    long move_y;

    if (!window) {
        return;
    }

    move_x = window->x;
    move_y = window->y;
    if (gwes_window_is_decorated_root(window)) {
        move_x += (long)GWES_DIALOG_BORDER_THICKNESS;
        move_y += (long)GWES_DIALOG_TITLE_HEIGHT;
    }

    gwes_send_window_message(window, WM_MOVE, 0UL, gwes_pack_signed_pair(move_x, move_y));
}

/*
 * Send one `WM_SIZE` notification using client-area dimensions.
 *
 * @param window Target window that resized.
 * @return Nothing.
 */
static void gwes_send_size_message(GwesWindowRecord* window) {
    if (!window) {
        return;
    }

    gwes_send_window_message(
        window,
        WM_SIZE,
        0UL,
        gwes_pack_signed_pair((long)gwes_window_client_width(window), (long)gwes_window_client_height(window)));
}

/*
 * Return the close-button rectangle in desktop coordinates.
 *
 * @param window Decorated top-level window.
 * @param x Receives the button X coordinate.
 * @param y Receives the button Y coordinate.
 * @return Non-zero when the rectangle is valid.
 */
static int gwes_close_button_origin(const GwesWindowRecord* window, unsigned long* x, unsigned long* y) {
    if (!gwes_window_is_decorated_root(window) || window->width <= (GWES_DIALOG_CLOSE_SIZE + (GWES_DIALOG_CLOSE_MARGIN * 2UL))) {
        return 0;
    }

    if (x) {
        *x = (unsigned long)window->x + window->width - GWES_DIALOG_CLOSE_MARGIN - GWES_DIALOG_CLOSE_SIZE;
    }
    if (y) {
        *y = (unsigned long)window->y + ((GWES_DIALOG_TITLE_HEIGHT > GWES_DIALOG_CLOSE_SIZE)
            ? ((GWES_DIALOG_TITLE_HEIGHT - GWES_DIALOG_CLOSE_SIZE) / 2UL)
            : 0UL);
    }
    return 1;
}

/*
 * Return the maximize-button rectangle in desktop coordinates.
 *
 * @param window Decorated top-level window.
 * @param x Receives the button X coordinate.
 * @param y Receives the button Y coordinate.
 * @return Non-zero when the rectangle is valid.
 */
static int gwes_maximize_button_origin(const GwesWindowRecord* window, unsigned long* x, unsigned long* y) {
    unsigned long close_x;
    unsigned long close_y;

    if (!gwes_close_button_origin(window, &close_x, &close_y)) {
        return 0;
    }

    if (close_x < (unsigned long)window->x + GWES_DIALOG_CONTROL_BUTTON_GAP + GWES_DIALOG_CLOSE_SIZE) {
        return 0;
    }

    if (x) {
        *x = close_x - GWES_DIALOG_CONTROL_BUTTON_GAP - GWES_DIALOG_CLOSE_SIZE;
    }
    if (y) {
        *y = close_y;
    }
    return 1;
}

/*
 * Return the minimize-button rectangle in desktop coordinates.
 *
 * @param window Decorated top-level window.
 * @param x Receives the button X coordinate.
 * @param y Receives the button Y coordinate.
 * @return Non-zero when the rectangle is valid.
 */
static int gwes_minimize_button_origin(const GwesWindowRecord* window, unsigned long* x, unsigned long* y) {
    unsigned long max_x;
    unsigned long max_y;

    if (!gwes_maximize_button_origin(window, &max_x, &max_y)) {
        return 0;
    }

    if (max_x < (unsigned long)window->x + GWES_DIALOG_CONTROL_BUTTON_GAP + GWES_DIALOG_CLOSE_SIZE) {
        return 0;
    }

    if (x) {
        *x = max_x - GWES_DIALOG_CONTROL_BUTTON_GAP - GWES_DIALOG_CLOSE_SIZE;
    }
    if (y) {
        *y = max_y;
    }
    return 1;
}

/*
 * Report whether one desktop point hits the server-drawn close button.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the close button was hit.
 */
static int gwes_window_hit_close_button(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long button_x = 0UL;
    unsigned long button_y = 0UL;

    if (!gwes_close_button_origin(window, &button_x, &button_y)) {
        return 0;
    }
    return x >= button_x
        && x < (button_x + GWES_DIALOG_CLOSE_SIZE)
        && y >= button_y
        && y < (button_y + GWES_DIALOG_CLOSE_SIZE);
}

/*
 * Report whether one desktop point hits the maximize controller button.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the maximize button was hit.
 */
static int gwes_window_hit_maximize_button(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long button_x = 0UL;
    unsigned long button_y = 0UL;

    if (!gwes_maximize_button_origin(window, &button_x, &button_y)) {
        return 0;
    }

    return x >= button_x
        && x < (button_x + GWES_DIALOG_CLOSE_SIZE)
        && y >= button_y
        && y < (button_y + GWES_DIALOG_CLOSE_SIZE);
}

/*
 * Report whether one desktop point hits the minimize controller button.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the minimize button was hit.
 */
static int gwes_window_hit_minimize_button(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long button_x = 0UL;
    unsigned long button_y = 0UL;

    if (!gwes_minimize_button_origin(window, &button_x, &button_y)) {
        return 0;
    }

    return x >= button_x
        && x < (button_x + GWES_DIALOG_CLOSE_SIZE)
        && y >= button_y
        && y < (button_y + GWES_DIALOG_CLOSE_SIZE);
}

/*
 * Report whether one desktop point hits the title bar but not the close box.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the title bar should begin a move drag.
 */
static int gwes_window_hit_title_bar(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    if (!gwes_window_is_decorated_root(window)) {
        return 0;
    }
    if (x < (unsigned long)window->x || x >= ((unsigned long)window->x + window->width)) {
        return 0;
    }
    if (y < (unsigned long)window->y || y >= ((unsigned long)window->y + GWES_DIALOG_TITLE_HEIGHT)) {
        return 0;
    }
    return !gwes_window_hit_close_button(window, x, y)
        && !gwes_window_hit_maximize_button(window, x, y)
        && !gwes_window_hit_minimize_button(window, x, y);
}

/*
 * Report which resize edges should react for one desktop point.
 *
 * @param window Decorated top-level window under the pointer.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return `ROS_KERNEL_GUI_WIDGET_RESIZE_*` mask.
 */
static unsigned long gwes_window_resize_edges(const GwesWindowRecord* window, unsigned long x, unsigned long y) {
    unsigned long edges = ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
    unsigned long left;
    unsigned long top;
    unsigned long right;
    unsigned long bottom;

    if (!gwes_window_is_decorated_root(window) || window->width == 0UL || window->height == 0UL) {
        return edges;
    }

    left = (unsigned long)window->x;
    top = (unsigned long)window->y;
    right = left + window->width;
    bottom = top + window->height;
    if (x < left || x >= right || y < top || y >= bottom) {
        return ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
    }

    if (x <= left + GWES_DIALOG_RESIZE_GRIP) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_LEFT;
    }
    else if (x + GWES_DIALOG_RESIZE_GRIP >= right) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_RIGHT;
    }
    if (y <= top + GWES_DIALOG_RESIZE_GRIP) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_TOP;
    }
    else if (y + GWES_DIALOG_RESIZE_GRIP >= bottom) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_BOTTOM;
    }

    return edges;
}

/*
 * Clear the current pointer drag or resize session.
 *
 * @return Nothing.
 */
static void gwes_clear_pointer_interaction(void) {
    gwes_render_clear_interaction_placeholder();
    g_gwes_pointer_interaction_hwnd = 0UL;
    g_gwes_pointer_capture_hwnd = 0UL;
    g_gwes_pointer_resize_edges = 0UL;
    g_gwes_pointer_drag_start_x = 0UL;
    g_gwes_pointer_drag_start_y = 0UL;
    g_gwes_pointer_drag_origin_x = 0L;
    g_gwes_pointer_drag_origin_y = 0L;
    g_gwes_pointer_drag_origin_width = 0UL;
    g_gwes_pointer_drag_origin_height = 0UL;
    g_gwes_pointer_drag_offset_x = 0L;
    g_gwes_pointer_drag_offset_y = 0L;
    g_gwes_pointer_preview_active = 0;
    g_gwes_pointer_preview_x = 0L;
    g_gwes_pointer_preview_y = 0L;
    g_gwes_pointer_preview_width = 0UL;
    g_gwes_pointer_preview_height = 0UL;
}

/*
 * Start one title-bar drag or border-resize interaction on a top-level window.
 *
 * @param window Target top-level window.
 * @param pointer_x Desktop X coordinate at interaction start.
 * @param pointer_y Desktop Y coordinate at interaction start.
 * @param resize_edges Non-zero for resize interactions, or zero for moves.
 * @return Nothing.
 */
static void gwes_begin_pointer_interaction(GwesWindowRecord* window, unsigned long pointer_x, unsigned long pointer_y, unsigned long resize_edges) {
    if (!window) {
        return;
    }

    g_gwes_pointer_interaction_hwnd = window->hwnd;
    g_gwes_pointer_capture_hwnd = window->hwnd;
    g_gwes_pointer_resize_edges = resize_edges;
    g_gwes_pointer_drag_start_x = pointer_x;
    g_gwes_pointer_drag_start_y = pointer_y;
    g_gwes_pointer_drag_origin_x = window->x;
    g_gwes_pointer_drag_origin_y = window->y;
    g_gwes_pointer_drag_origin_width = window->width;
    g_gwes_pointer_drag_origin_height = window->height;
    g_gwes_pointer_drag_offset_x = (long)pointer_x - window->x;
    g_gwes_pointer_drag_offset_y = (long)pointer_y - window->y;
    gwes_set_pointer_interaction_preview(window->x, window->y, window->width, window->height);
}

/*
 * Deliver one synthetic `WM_MOUSELEAVE` when the hover target changes.
 *
 * @param next_hwnd Window now under the pointer, or zero when none is hovered.
 * @param buttons Pointer button mask.
 * @param x Current desktop X coordinate.
 * @param y Current desktop Y coordinate.
 * @return Nothing.
 */
static void gwes_update_hover(unsigned long next_hwnd, unsigned long buttons, unsigned long x, unsigned long y) {
    GwesWindowRecord* previous;
    long local_x = 0L;
    long local_y = 0L;

    if (g_gwes_hover_hwnd == next_hwnd) {
        return;
    }

    previous = gwes_find_window_any(g_gwes_hover_hwnd);
    if (previous != NULL) {
        if (gwes_render_translate_pointer(previous->hwnd, x, y, &local_x, &local_y) != 0) {
            local_x = 0L;
            local_y = 0L;
        }
        gwes_send_window_message(previous, WM_MOUSELEAVE, buttons, gwes_pack_signed_pair(local_x, local_y));
    }

    g_gwes_hover_hwnd = next_hwnd;
}

/*
 * Move one top-level window through the retained renderer and mirror `WM_MOVE`.
 *
 * @param window Target window being dragged.
 * @param x Requested outer-frame X coordinate.
 * @param y Requested outer-frame Y coordinate.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_move_window_record(GwesWindowRecord* window, long x, long y) {
    long status;

    if (!window) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    status = gwes_render_move_window(window->hwnd, x, y);
    if (status < 0L) {
        return status;
    }

    window->x = x;
    window->y = y;
    if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_NORMAL) {
        gwes_store_restore_geometry(window);
    }
    else if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_MINIMIZED && window->restore_valid) {
        window->restore_x = x;
        window->restore_y = y;
    }
    gwes_send_move_message(window);
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Resize one top-level window through the retained renderer and mirror the
 * Win32-style `WM_MOVE`/`WM_SIZE` notifications the client message loop
 * expects.
 *
 * @param window Target window being resized.
 * @param x Requested outer-frame X coordinate.
 * @param y Requested outer-frame Y coordinate.
 * @param width Requested outer-frame width.
 * @param height Requested outer-frame height.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_resize_window_record(GwesWindowRecord* window, long x, long y, unsigned long width, unsigned long height) {
    long status;
    int moved;
    int resized;

    if (!window) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    moved = window->x != x || window->y != y;
    resized = window->width != width || window->height != height;
    if (!moved && !resized) {
        return ROS_USER_IPC_STATUS_OK;
    }

    if (moved && !resized) {
        return gwes_move_window_record(window, x, y);
    }

    status = gwes_render_resize_window(window->hwnd, x, y, width, height);
    if (status < 0L) {
        return status;
    }

    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    if (moved) {
        gwes_send_move_message(window);
    }
    if (resized) {
        gwes_send_size_message(window);
    }
    if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_NORMAL) {
        gwes_store_restore_geometry(window);
    }
    else if (gwes_window_is_decorated_root(window) && window->show_state == GWES_WINDOW_SHOW_MINIMIZED && window->restore_valid) {
        window->restore_x = x;
        window->restore_y = y;
    }

    gwes_publish_shell_state();

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Commit one placeholder move or resize into the live retained-window state.
 *
 * The preview path avoids client traffic while the pointer is still moving,
 * and this helper performs the single real geometry mutation once the user has
 * finished the interaction.
 *
 * @return Nothing.
 */
static void gwes_commit_pointer_interaction(void) {
    GwesWindowRecord* window;

    if (g_gwes_pointer_interaction_hwnd == 0UL || !g_gwes_pointer_preview_active) {
        return;
    }

    window = gwes_find_window_any(g_gwes_pointer_interaction_hwnd);
    if (window == NULL) {
        return;
    }

    if (g_gwes_pointer_resize_edges != ROS_KERNEL_GUI_WIDGET_RESIZE_NONE) {
        (void)gwes_resize_window_record(
            window,
            g_gwes_pointer_preview_x,
            g_gwes_pointer_preview_y,
            g_gwes_pointer_preview_width,
            g_gwes_pointer_preview_height);
    }
    else {
        (void)gwes_move_window_record(window, g_gwes_pointer_preview_x, g_gwes_pointer_preview_y);
    }
}

/*
 * Toggle one top-level window between normal and maximized geometry.
 *
 * @param window Decorated top-level window record.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_toggle_maximize_window(GwesWindowRecord* window) {
    unsigned long desktop_width = 0UL;
    unsigned long desktop_height = 0UL;
    long desktop_x = 0L;
    long desktop_y = 0L;
    unsigned long previous_state;
    long status;

    if (!gwes_window_is_decorated_root(window)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED) {
        if (!window->restore_valid) {
            return ROS_USER_IPC_STATUS_OK;
        }

        window->show_state = GWES_WINDOW_SHOW_NORMAL;
        status = gwes_resize_window_record(window, window->restore_x, window->restore_y, window->restore_width, window->restore_height);
        if (status < 0L) {
            window->show_state = GWES_WINDOW_SHOW_MAXIMIZED;
            return status;
        }
        return ROS_USER_IPC_STATUS_OK;
    }

    if (gwes_window_requests_fullscreen(window)) {
        if (gwes_render_query_desktop_size(&desktop_width, &desktop_height) < 0L) {
            return ROS_USER_IPC_STATUS_NOT_FOUND;
        }
    }
    else if (gwes_query_shell_work_area(&desktop_x, &desktop_y, &desktop_width, &desktop_height) < 0L) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    if (window->show_state == GWES_WINDOW_SHOW_NORMAL || !window->restore_valid) {
        gwes_store_restore_geometry(window);
    }

    previous_state = window->show_state;
    window->show_state = GWES_WINDOW_SHOW_MAXIMIZED;
    status = gwes_resize_window_record(window, desktop_x, desktop_y, desktop_width, desktop_height);
    if (status < 0L) {
        window->show_state = previous_state;
        return status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Toggle one top-level window between normal/maximized geometry and a shaded
 * title-bar-only minimized state that remains recoverable without a taskbar.
 *
 * @param window Decorated top-level window record.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_toggle_minimize_window(GwesWindowRecord* window) {
    long status;
    unsigned long previous_state;
    long target_x;
    long target_y;
    unsigned long target_width;

    if (!gwes_window_is_decorated_root(window)) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    if (window->show_state == GWES_WINDOW_SHOW_MINIMIZED) {
        if (window->minimized_restore_state == GWES_WINDOW_SHOW_MAXIMIZED) {
            return gwes_toggle_maximize_window(window);
        }
        if (!window->restore_valid) {
            window->show_state = GWES_WINDOW_SHOW_NORMAL;
            return ROS_USER_IPC_STATUS_OK;
        }

        window->show_state = GWES_WINDOW_SHOW_NORMAL;
        status = gwes_resize_window_record(window, window->restore_x, window->restore_y, window->restore_width, window->restore_height);
        if (status < 0L) {
            window->show_state = GWES_WINDOW_SHOW_MINIMIZED;
            return status;
        }
        return ROS_USER_IPC_STATUS_OK;
    }

    if (window->show_state == GWES_WINDOW_SHOW_NORMAL || !window->restore_valid) {
        gwes_store_restore_geometry(window);
    }

    target_x = (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED && window->restore_valid) ? window->restore_x : window->x;
    target_y = (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED && window->restore_valid) ? window->restore_y : window->y;
    target_width = (window->show_state == GWES_WINDOW_SHOW_MAXIMIZED && window->restore_valid) ? window->restore_width : window->width;

    previous_state = window->show_state;
    window->minimized_restore_state = previous_state;
    window->show_state = GWES_WINDOW_SHOW_MINIMIZED;
    status = gwes_resize_window_record(window, target_x, target_y, target_width, GWES_DIALOG_TITLE_HEIGHT + GWES_DIALOG_BORDER_THICKNESS);
    if (status < 0L) {
        window->show_state = previous_state;
        return status;
    }

    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Resolve the window that should currently receive pointer input.
 *
 * Pointer capture wins when it is active, because the capturing window must
 * continue receiving drag traffic even if the pointer visually leaves its
 * client area. When capture is absent, GWES falls back to the top-most visible
 * window under the desktop pointer location.
 *
 * @param x Desktop pointer X coordinate.
 * @param y Desktop pointer Y coordinate.
 * @param local_x Receives the client-relative X coordinate when a target exists.
 * @param local_y Receives the client-relative Y coordinate when a target exists.
 * @return Target server window record, or null when no visible window was hit.
 */
static GwesWindowRecord* gwes_resolve_pointer_target(unsigned long x, unsigned long y, long* local_x, long* local_y) {
    GwesWindowRecord* target = NULL;
    unsigned long target_hwnd = 0UL;
    long resolved_local_x = 0L;
    long resolved_local_y = 0L;

    if (local_x) {
        *local_x = 0L;
    }
    if (local_y) {
        *local_y = 0L;
    }

    if (g_gwes_pointer_capture_hwnd != 0UL) {
        target = gwes_find_window_any(g_gwes_pointer_capture_hwnd);
        if (target != NULL) {
            target_hwnd = target->hwnd;
            if (gwes_render_translate_pointer(target_hwnd, x, y, &resolved_local_x, &resolved_local_y) != 0) {
                target = NULL;
                target_hwnd = 0UL;
            }
        }
        else {
            g_gwes_pointer_capture_hwnd = 0UL;
        }
    }

    if (target == NULL && gwes_render_hit_test(x, y, &target_hwnd, &resolved_local_x, &resolved_local_y) == 0) {
        target = gwes_find_window_any(target_hwnd);
    }

    if (target != NULL) {
        if (local_x) {
            *local_x = resolved_local_x;
        }
        if (local_y) {
            *local_y = resolved_local_y;
        }
    }

    return target;
}

/*
 * Resolve the effective cursor asset for one window subtree.
 *
 * Child controls are allowed to inherit their parent's cursor policy, so GWES
 * walks up the parent chain until it finds an explicit override. When none is
 * present, the renderer falls back to the default arrow asset.
 *
 * @param window Window under the pointer, or null when nothing is hovered.
 * @return DOS-style `.cur32` path that should drive the visible cursor.
 */
static const char* gwes_effective_cursor_path(GwesWindowRecord* window) {
    GwesWindowRecord* current = window;

    while (current != NULL) {
        if (current->cursor_path[0] != '\0') {
            return current->cursor_path;
        }
        if (current->parent == 0UL) {
            break;
        }
        current = gwes_find_window_any(current->parent);
    }

    return ROS_WINDOW_CURSOR_DEFAULT_PATH;
}

/*
 * Push the current GWES pointer state into the compositor.
 *
 * Recomputing this in one helper keeps cursor refreshes consistent for pointer
 * motion, cursor-property changes, and window teardown. That avoids stale
 * cursor overlays when the hovered window disappears without another input
 * event arriving immediately afterwards.
 *
 * @return Nothing.
 */
static void gwes_refresh_pointer_cursor(void) {
    GwesWindowRecord* target = NULL;

    if (g_gwes_pointer_visible) {
        target = gwes_resolve_pointer_target(g_gwes_pointer_x, g_gwes_pointer_y, NULL, NULL);
    }

    gwes_refresh_pointer_cursor_for_target(target);
}

/*
 * Push one already-resolved cursor target into the compositor.
 *
 * Pointer-heavy paths often already know which window sits under the cursor.
 * Reusing that target avoids a second full hit-test pass for the same sample,
 * which becomes noticeable once several windows are open.
 *
 * @param target Window currently under the pointer, or null for the desktop.
 * @return Nothing.
 */
static void gwes_refresh_pointer_cursor_for_target(GwesWindowRecord* target) {
    const char* cursor_path = ROS_WINDOW_CURSOR_DEFAULT_PATH;

    if (g_gwes_pointer_visible) {
        cursor_path = gwes_effective_cursor_path(target);
    }

    gwes_render_update_pointer(g_gwes_pointer_x, g_gwes_pointer_y, g_gwes_pointer_visible, cursor_path);
}

/*
 * Adopt the kernel's last published pointer snapshot so GWES starts from the
 * real desktop pointer state rather than a renderer-local fallback position.
 *
 * This keeps the first rendered cursor frame aligned with the input backend's
 * current coordinates and avoids waiting for a fresh motion packet after GWES
 * attaches to the shared-input view.
 *
 * @param pointer_state Kernel-owned pointer snapshot.
 * @return Nothing.
 */
static void gwes_sync_pointer_snapshot(const RosKernelGuiPointerState* pointer_state) {
    if (!pointer_state) {
        return;
    }

    g_gwes_pointer_x = pointer_state->x;
    g_gwes_pointer_y = pointer_state->y;
    g_gwes_pointer_visible = pointer_state->visible != 0U;
    gwes_refresh_pointer_cursor();
}

/*
 * Acquire the fixed shared-input view published by the kernel GUI service.
 *
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_shared_input_acquire(void) {
    RosKernelGuiSharedInputView view;
    long status;

    memset(&view, 0, sizeof(view));
    status = controlGui(ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE, (unsigned long)&view);
    if (status < 0) {
        return status;
    }
    if (view.version != ROS_KERNEL_GUI_SHARED_INPUT_VERSION
        || view.view_address == 0ULL
        || view.view_size < sizeof(RosKernelGuiSharedInputRegion)
        || view.consumer_index >= ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    g_gwes_shared_input = (RosKernelGuiSharedInputRegion*)(uintptr_t)view.view_address;
    g_gwes_shared_input_consumer_index = view.consumer_index;
    g_gwes_shared_input_attached = 1;
    gwes_shared_input_barrier();
    if (g_gwes_shared_input->last_pointer_state.visible != 0U) {
        gwes_sync_pointer_snapshot(&g_gwes_shared_input->last_pointer_state);
    }
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Release the shared-input mapping during shutdown or reattach.
 *
 * @return Nothing.
 */
static void gwes_shared_input_release(void) {
    if (!g_gwes_shared_input_attached) {
        return;
    }

    (void)controlGui(ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_RELEASE, 0UL);
    g_gwes_shared_input = NULL;
    g_gwes_shared_input_consumer_index = 0UL;
    g_gwes_shared_input_attached = 0;
}

/*
 * Deliver one keyboard event to the focused or captured window.
 *
 * @param event Shared-input event copied from the ring buffer.
 * @return Nothing.
 */
static void gwes_dispatch_keyboard_event(const RosKernelGuiInputEvent* event) {
    GwesWindowRecord* target;
    unsigned long message;
    unsigned long target_hwnd = g_gwes_keyboard_capture_hwnd != 0UL ? g_gwes_keyboard_capture_hwnd : g_gwes_focus_hwnd;

    if (!event || target_hwnd == 0UL) {
        return;
    }

    target = gwes_find_window_any(target_hwnd);
    if (!target) {
        gwes_forget_window_state(target_hwnd);
        return;
    }

    message = (event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) ? WM_KEYUP : WM_KEYDOWN;
    gwes_send_window_message(target, message, event->key, 0UL);
}

/*
 * Deliver one pointer event to the window under the pointer or the capture owner.
 *
 * @param event Shared-input event copied from the ring buffer.
 * @return Nothing.
 */
static void gwes_dispatch_pointer_event(const RosKernelGuiInputEvent* event) {
    GwesWindowRecord* target = NULL;
    GwesWindowRecord* root = NULL;
    GwesWindowRecord* previous_focus_root = NULL;
    GwesWindowRecord* pressed_target = NULL;
    long local_x = 0L;
    long local_y = 0L;
    long pressed_local_x = 0L;
    long pressed_local_y = 0L;
    unsigned long message;

    if (!event) {
        return;
    }

    g_gwes_pointer_x = event->x;
    g_gwes_pointer_y = event->y;

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE) {
        g_gwes_pointer_visible = 0;
        g_gwes_desktop_pointer_down = 0;
        gwes_update_hover(0UL, event->buttons, event->x, event->y);
        gwes_clear_pointer_interaction();
        gwes_refresh_pointer_cursor();
        return;
    }

    g_gwes_pointer_visible = 1;

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE && g_gwes_pointer_interaction_hwnd != 0UL) {
        target = gwes_find_window_any(g_gwes_pointer_interaction_hwnd);
        gwes_refresh_pointer_cursor_for_target(target);
        if (target == NULL) {
            gwes_clear_pointer_interaction();
            return;
        }

        if (g_gwes_pointer_resize_edges != ROS_KERNEL_GUI_WIDGET_RESIZE_NONE) {
            long next_x = g_gwes_pointer_drag_origin_x;
            long next_y = g_gwes_pointer_drag_origin_y;
            unsigned long next_width = g_gwes_pointer_drag_origin_width;
            unsigned long next_height = g_gwes_pointer_drag_origin_height;
            long delta_x = (long)event->x - (long)g_gwes_pointer_drag_start_x;
            long delta_y = (long)event->y - (long)g_gwes_pointer_drag_start_y;

            if ((g_gwes_pointer_resize_edges & ROS_KERNEL_GUI_WIDGET_RESIZE_LEFT) != 0UL) {
                long right_edge = g_gwes_pointer_drag_origin_x + (long)g_gwes_pointer_drag_origin_width;
                long proposed_x = g_gwes_pointer_drag_origin_x + delta_x;
                long proposed_width = right_edge - proposed_x;

                if (proposed_width < (long)GWES_DECORATED_MIN_WIDTH) {
                    proposed_width = (long)GWES_DECORATED_MIN_WIDTH;
                }
                next_width = (unsigned long)proposed_width;
                next_x = right_edge - proposed_width;
            }
            if ((g_gwes_pointer_resize_edges & ROS_KERNEL_GUI_WIDGET_RESIZE_RIGHT) != 0UL) {
                long proposed_width = (long)g_gwes_pointer_drag_origin_width + delta_x;

                if (proposed_width < (long)GWES_DECORATED_MIN_WIDTH) {
                    proposed_width = (long)GWES_DECORATED_MIN_WIDTH;
                }
                next_width = (unsigned long)proposed_width;
            }
            if ((g_gwes_pointer_resize_edges & ROS_KERNEL_GUI_WIDGET_RESIZE_TOP) != 0UL) {
                long bottom_edge = g_gwes_pointer_drag_origin_y + (long)g_gwes_pointer_drag_origin_height;
                long proposed_y = g_gwes_pointer_drag_origin_y + delta_y;
                long proposed_height = bottom_edge - proposed_y;

                if (proposed_height < (long)GWES_DECORATED_MIN_HEIGHT) {
                    proposed_height = (long)GWES_DECORATED_MIN_HEIGHT;
                }
                next_height = (unsigned long)proposed_height;
                next_y = bottom_edge - proposed_height;
            }
            if ((g_gwes_pointer_resize_edges & ROS_KERNEL_GUI_WIDGET_RESIZE_BOTTOM) != 0UL) {
                long proposed_height = (long)g_gwes_pointer_drag_origin_height + delta_y;

                if (proposed_height < (long)GWES_DECORATED_MIN_HEIGHT) {
                    proposed_height = (long)GWES_DECORATED_MIN_HEIGHT;
                }
                next_height = (unsigned long)proposed_height;
            }

            gwes_set_pointer_interaction_preview(next_x, next_y, next_width, next_height);
        }
        else {
            gwes_set_pointer_interaction_preview(
                (long)event->x - g_gwes_pointer_drag_offset_x,
                (long)event->y - g_gwes_pointer_drag_offset_y,
                target->width,
                target->height);
        }
        return;
    }

    target = gwes_resolve_pointer_target(event->x, event->y, &local_x, &local_y);
    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE) {
        target = gwes_filter_pointer_move_target(target);
    }
    gwes_refresh_pointer_cursor_for_target(target);

    switch (event->type) {
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE:
        gwes_update_hover(target != NULL ? target->hwnd : 0UL, event->buttons, event->x, event->y);
        if (target == NULL) {
            return;
        }
        message = WM_MOUSEMOVE;
        break;
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN:
        gwes_update_hover(target != NULL ? target->hwnd : 0UL, event->buttons, event->x, event->y);
        if (target == NULL) {
            g_gwes_desktop_pointer_down = 1;
            gwes_send_shell_ipc(
                ROS_EXPLORER_SHELL_IPC_KIND_DESKTOP_CLICK,
                event->x,
                event->y,
                event->buttons,
                0UL);
            return;
        }
        g_gwes_desktop_pointer_down = 0;
        message = WM_LBUTTONDOWN;
        break;
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP:
        gwes_update_hover(target != NULL ? target->hwnd : 0UL, event->buttons, event->x, event->y);
        if (target == NULL) {
            g_gwes_desktop_pointer_down = 0;
        }
        message = WM_LBUTTONUP;
        break;
    default:
        return;
    }

    if (message == WM_LBUTTONDOWN) {
        root = gwes_find_root_window(target);
        previous_focus_root = gwes_find_root_window(gwes_find_window_any(g_gwes_focus_hwnd));

        gwes_set_focus(target->hwnd);
        g_gwes_pointer_down_hwnd = target->hwnd;
        if (root != NULL && (previous_focus_root == NULL || previous_focus_root->hwnd != root->hwnd)) {
            (void)gwes_render_raise_window(root->hwnd);
        }

        if (root != NULL && target->hwnd == root->hwnd) {
            unsigned long resize_edges = gwes_window_resize_edges(root, event->x, event->y);

            if (gwes_window_hit_close_button(root, event->x, event->y)) {
                gwes_send_window_message(root, WM_CLOSE, 0UL, 0UL);
            }
            else if (gwes_window_hit_maximize_button(root, event->x, event->y)) {
                (void)gwes_toggle_maximize_window(root);
            }
            else if (gwes_window_hit_minimize_button(root, event->x, event->y)) {
                (void)gwes_toggle_minimize_window(root);
            }
            else if (resize_edges != ROS_KERNEL_GUI_WIDGET_RESIZE_NONE) {
                if (root->show_state != GWES_WINDOW_SHOW_MAXIMIZED) {
                    gwes_begin_pointer_interaction(root, event->x, event->y, resize_edges);
                }
            }
            else if (gwes_window_hit_title_bar(root, event->x, event->y)) {
                if (root->show_state != GWES_WINDOW_SHOW_MAXIMIZED) {
                    gwes_begin_pointer_interaction(root, event->x, event->y, ROS_KERNEL_GUI_WIDGET_RESIZE_NONE);
                }
            }
        }
    }

    if (target != NULL) {
        gwes_send_window_message(target, message, event->buttons, gwes_pack_signed_pair(local_x, local_y));
    }
    if (message == WM_LBUTTONUP) {
        pressed_target = gwes_find_window_any(g_gwes_pointer_down_hwnd);
        if (pressed_target != NULL && (target == NULL || pressed_target->hwnd != target->hwnd)) {
            if (gwes_render_translate_pointer(pressed_target->hwnd, event->x, event->y, &pressed_local_x, &pressed_local_y) != 0L) {
                pressed_local_x = 0L;
                pressed_local_y = 0L;
            }
            gwes_send_window_message(pressed_target, WM_LBUTTONUP, event->buttons, gwes_pack_signed_pair(pressed_local_x, pressed_local_y));
        }
        if (target != NULL && g_gwes_pointer_down_hwnd == target->hwnd && g_gwes_pointer_interaction_hwnd == 0UL) {
            gwes_send_window_message(target, WM_MOUSECLICKED, event->buttons, gwes_pack_signed_pair(local_x, local_y));
        }
        g_gwes_pointer_down_hwnd = 0UL;
        gwes_commit_pointer_interaction();
        gwes_clear_pointer_interaction();
    }
}

/*
 * Drain any shared-input records that became visible since the last GWES poll.
 *
 * @return Count of consumed input events.
 */
unsigned long gwes_consume_shared_input(void) {
    volatile RosKernelGuiSharedInputRegion* region;
    volatile RosKernelGuiSharedInputConsumer* consumer;
    volatile RosKernelGuiSharedInputRecord* shared_record;
    unsigned long now;
    unsigned long processed_count = 0UL;
    uint64_t head_sequence;
    uint64_t tail_sequence;

    if (!g_gwes_shared_input_attached) {
        if (gwes_shared_input_acquire() < 0L) {
            return 0UL;
        }
    }

    region = (volatile RosKernelGuiSharedInputRegion*)g_gwes_shared_input;
    consumer = &region->consumers[g_gwes_shared_input_consumer_index];
    now = getUptimeMs();

    gwes_shared_input_barrier();
    tail_sequence = region->tail_sequence;
    head_sequence = consumer->head_sequence;
    if (head_sequence + ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY < tail_sequence) {
        consumer->drop_count += (tail_sequence - ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY) - head_sequence;
        head_sequence = tail_sequence - ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY;
    }

    while (head_sequence < tail_sequence) {
        RosKernelGuiInputEvent event;

        shared_record = &region->records[head_sequence % ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY];
        gwes_shared_input_barrier();
        if (shared_record->sequence != head_sequence || shared_record->event.version != ROS_KERNEL_GUI_INPUT_EVENT_VERSION) {
            consumer->drop_count++;
            ++head_sequence;
            continue;
        }
        event.version = shared_record->event.version;
        event.type = shared_record->event.type;
        event.x = shared_record->event.x;
        event.y = shared_record->event.y;
        event.key = shared_record->event.key;
        event.buttons = shared_record->event.buttons;
        event.reserved = shared_record->event.reserved;

        if (gwes_window_queue_push_input_event(&event) < 0L) {
            consumer->drop_count++;
            ++head_sequence;
            continue;
        }

        ++head_sequence;
        ++processed_count;
        // Presenting only after the entire shared-input backlog is drained makes
        // pointer-heavy operations like window dragging feel sticky because the
        // preview rectangle can update dozens of times before one frame reaches
        // the display. Stop after a bounded batch so the outer loop can present
        // damage and then come back for the rest of the backlog.
        if (processed_count >= GWES_INPUT_BATCH_LIMIT) {
            break;
        }
    }

    consumer->head_sequence = head_sequence;
    consumer->last_seen_msec = (unsigned int)now;
    return processed_count;
}

/*
 * Release every class and window record currently owned by one client process.
 *
 * @param owner_pid Client process identifier whose state should be purged.
 * @return Counts of released records for logging and diagnostics.
 */
static GwesCleanupSummary gwes_release_client_objects(long owner_pid) {
    GwesCleanupSummary summary = { 0UL, 0UL };
    GwesClassRecord* class_record = g_gwes_classes;
    GwesWindowRecord* window = g_gwes_windows;

    while (class_record != NULL) {
        GwesClassRecord* next = class_record->next;

        if (!class_record->in_use || class_record->owner_pid != owner_pid) {
            class_record = next;
            continue;
        }

        gwes_release_class_record(class_record);
        ++summary.released_classes;
        class_record = next;
    }

    while (window != NULL) {
        GwesWindowRecord* next = window->next;

        if (!window->in_use || window->owner_pid != owner_pid) {
            window = next;
            continue;
        }

        gwes_release_window_record(window, 1);
        ++summary.released_windows;
        window = next;
    }

    gwes_refresh_pointer_cursor();

    return summary;
}

/*
 * Reap any client records that survived a process death without an explicit
 * quit notification.
 *
 * @return Nothing.
 */
static void gwes_reap_dead_clients(void) {
    GwesWindowRecord* window = g_gwes_windows;

    while (window != NULL) {
        GwesCleanupSummary summary;
        long owner_pid;
        GwesWindowRecord* next = window->next;

        if (!window->in_use) {
            window = next;
            continue;
        }

        owner_pid = window->owner_pid;
        if (gwes_pid_is_live(owner_pid)) {
            window = next;
            continue;
        }

        summary = gwes_release_client_objects(owner_pid);
        if (summary.released_classes != 0UL || summary.released_windows != 0UL) {
            gwes_log_cleanup(owner_pid, 0UL, "process-dead", summary);
        }

        window = next;
    }
}

/*
 * Dispatch one queued shared-input event on the window/render owner thread.
 *
 * @param event Shared-input event copied from the queue.
 * @return Nothing.
 */
void gwes_window_thread_handle_input_event(const RosKernelGuiInputEvent* event) {
    if (event == NULL) {
        return;
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN || event->type == ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP) {
        gwes_dispatch_keyboard_event(event);
    }
    else {
        gwes_dispatch_pointer_event(event);
    }
}

/*
 * Reap dead client state from the window/render owner thread.
 *
 * @return Nothing.
 */
void gwes_window_thread_reap_dead_clients(void) {
    gwes_reap_dead_clients();
}

/*
 * Decode two signed 32-bit values packed into one IPC scalar.
 *
 * @param packed Packed pair encoded by the client-side window helper.
 * @param first Receives the upper signed 32-bit value.
 * @param second Receives the lower signed 32-bit value.
 * @return Nothing.
 */
static void gwes_unpack_signed_pair(unsigned long packed, long* first, long* second) {
    if (first) {
        *first = (long)(int32_t)((packed >> 32) & 0xFFFFFFFFUL);
    }
    if (second) {
        *second = (long)(int32_t)(packed & 0xFFFFFFFFUL);
    }
}
/*
 * Send one reply or delivered message to a client process.
 *
 * @param receiver_pid Destination client PID.
 * @param kind GWES protocol packet kind.
 * @param arg0 First scalar field.
 * @param arg1 Second scalar field.
 * @param arg2 Third scalar field.
 * @param arg3 Fourth scalar field.
 * @param text Optional primary text field.
 * @param text2 Optional secondary text field.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_send_packet(long receiver_pid, unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3, const char* text, const char* text2) {
    UserIpcMessage packet;

    memset(&packet, 0, sizeof(packet));
    packet.protocol = ROS_WINDOW_SERVER_PROTOCOL;
    packet.kind = kind;
    packet.arg0 = arg0;
    packet.arg1 = arg1;
    packet.arg2 = arg2;
    packet.arg3 = arg3;
    gwes_copy_text(packet.text, sizeof(packet.text), text);
    gwes_copy_text(packet.text2, sizeof(packet.text2), text2);
    return sendUserIpcMessage(receiver_pid, &packet);
}

/*
 * Mirror one window message back to its owning client process.
 *
 * @param window Target server window record.
 * @param message Window message identifier.
 * @param wparam First payload word.
 * @param lparam Second payload word.
 * @return Nothing.
 */
static void gwes_send_window_message(GwesWindowRecord* window, unsigned long message, unsigned long wparam, unsigned long lparam) {
    if (!window || !window->in_use) {
        return;
    }

    if (!gwes_message_prefers_post(message)) {
        return;
    }

    /*
     * Do not drop `WM_MOUSEMOVE` here.
     *
     * The client runtime already coalesces queued move messages after they
     * cross the IPC boundary. A second 16 ms server-side drop stage makes
     * hover and drag feedback feel artificially sticky while providing little
     * extra protection against queue growth.
     */

    if (gwes_send_packet(window->owner_pid, ROS_WINDOW_SERVER_KIND_DELIVER_MESSAGE, window->hwnd, message, wparam, lparam, NULL, NULL) < 0) {
        GwesCleanupSummary summary = gwes_release_client_objects(window->owner_pid);

        if (summary.released_classes != 0UL || summary.released_windows != 0UL) {
            gwes_log_cleanup(window->owner_pid, 0UL, "deliver-failed", summary);
        }
    }
}

/*
 * Record one class registration request from a client and acknowledge it.
 *
 * @param packet Client request packet.
 * @return Nothing.
 */
static void gwes_handle_register_class(const UserIpcMessage* packet) {
    GwesClassRecord* record;

    if (!packet) {
        return;
    }

    gwes_log_client_request("register-class", packet);

    record = gwes_find_class(packet->sender_pid, packet->text);
    if (record == NULL) {
        record = gwes_allocate_class();
    }
    if (record == NULL) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_REGISTER_CLASS_ACK, (unsigned long)ROS_USER_IPC_STATUS_NO_SPACE, 0UL, 0UL, 0UL, packet->text, NULL);
        return;
    }

    record->in_use = 1;
    record->owner_pid = packet->sender_pid;
    gwes_copy_text(record->class_name, sizeof(record->class_name), packet->text);
    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_REGISTER_CLASS_ACK, 0UL, 0UL, 0UL, 0UL, packet->text, NULL);
}

/*
 * Create one server window record and send both the create reply and the
 * initial `WM_CREATE` message back to the client.
 *
 * @param packet Client create-window request.
 * @return Nothing.
 */
static void gwes_handle_create_window(const UserIpcMessage* packet) {
    GwesClassRecord* class_record;
    GwesWindowRecord* window;
    unsigned long width;
    unsigned long height;
    long x;
    long y;
    long render_status;

    if (!packet) {
        return;
    }

    gwes_log_client_request("create-window", packet);

    class_record = gwes_find_class(packet->sender_pid, packet->text);
    if (class_record == NULL) {
        gwes_log_create_failure("class-not-found", packet, ROS_USER_IPC_STATUS_NOT_FOUND);
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY, 0UL, (unsigned long)ROS_USER_IPC_STATUS_NOT_FOUND, 0UL, 0UL, packet->text, packet->text2);
        return;
    }

    window = gwes_allocate_window();
    if (window == NULL) {
        gwes_log_create_failure("server-window-alloc-failed", packet, ROS_USER_IPC_STATUS_NO_SPACE);
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY, 0UL, (unsigned long)ROS_USER_IPC_STATUS_NO_SPACE, 0UL, 0UL, packet->text, packet->text2);
        return;
    }

    window->in_use = 1;
    window->owner_pid = packet->sender_pid;
    window->hwnd = g_gwes_next_hwnd++;
    window->parent = packet->arg0;
    gwes_unpack_signed_pair(packet->arg1, &x, &y);
    gwes_unpack_pair(packet->arg2, &width, &height);
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->style = packet->arg3;
    window->show_state = GWES_WINDOW_SHOW_NORMAL;
    window->minimized_restore_state = GWES_WINDOW_SHOW_NORMAL;
    gwes_copy_text(window->class_name, sizeof(window->class_name), packet->text);
    gwes_copy_text(window->title, sizeof(window->title), packet->text2);
    if (gwes_window_is_decorated_root(window)) {
        gwes_store_restore_geometry(window);
    }

    render_status = gwes_render_create_window(window->hwnd, packet->sender_pid, window->parent, window->x, window->y, window->width, window->height, window->style, window->class_name, window->title);
    if (render_status != 0) {
        gwes_log_create_failure("render-create-window", packet, render_status);
        gwes_release_window_record(window, 0);
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY, 0UL, (unsigned long)render_status, 0UL, 0UL, packet->text, packet->text2);
        return;
    }

    if (gwes_window_is_task_candidate(window)) {
        gwes_set_focus(window->hwnd);
        (void)gwes_render_raise_window(window->hwnd);
    }
    gwes_render_refresh_window_groups();
    gwes_publish_shell_state();

    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_CREATE_WINDOW_REPLY, window->hwnd, 0UL, 0UL, 0UL, window->class_name, window->title);
    gwes_send_window_message(window, WM_CREATE, 0UL, 0UL);
}

/*
 * Consume one explicit process-quit notification from the client helper DLL.
 *
 * @param packet Client quit packet containing the exit code in `arg0`.
 * @return Nothing.
 */
static void gwes_handle_process_quit(const UserIpcMessage* packet) {
    GwesCleanupSummary summary;

    if (!packet) {
        return;
    }

    summary = gwes_release_client_objects(packet->sender_pid);
    gwes_log_cleanup(packet->sender_pid, packet->arg0, "post-quit", summary);
}

/*
 * Decode two 32-bit values that were packed into one 64-bit IPC field.
 *
 * @param packed Packed pair encoded by the client-side drawing library.
 * @param first Receives the upper 32 bits.
 * @param second Receives the lower 32 bits.
 * @return Nothing.
 */
static void gwes_unpack_pair(unsigned long packed, unsigned long* first, unsigned long* second) {
    if (first) {
        *first = (packed >> 32) & 0xFFFFFFFFUL;
    }
    if (second) {
        *second = packed & 0xFFFFFFFFUL;
    }
}

/*
 * Mark one client-owned surface rectangle dirty after local GDI rendering.
 *
 * GDI now writes directly into the shared window surface, so GWES only needs
 * to learn which rectangle changed and schedule it for composition.
 *
 * @param packet Client invalidate packet.
 * @return Nothing.
 */
static void gwes_handle_invalidate_window(const UserIpcMessage* packet) {
    GwesWindowRecord* window;
    unsigned long x;
    unsigned long y;
    unsigned long width;
    unsigned long height;

    if (!packet) {
        return;
    }

    window = gwes_find_window(packet->sender_pid, packet->arg0);
    if (!window) {
        return;
    }

    gwes_unpack_pair(packet->arg1, &x, &y);
    gwes_unpack_pair(packet->arg2, &width, &height);
    if (width == 0UL || height == 0UL) {
        return;
    }

    gwes_render_mark_window_dirty(window->hwnd, x, y, width, height);
}

/*
 * Consume one explicit move/resize request for a live client-owned window.
 *
 * @param packet Client request packet.
 * @return Nothing.
 */
static void gwes_handle_set_window_bounds(const UserIpcMessage* packet) {
    GwesWindowRecord* window;
    unsigned long previous_state = GWES_WINDOW_SHOW_NORMAL;
    long x = 0L;
    long y = 0L;
    unsigned long width = 0UL;
    unsigned long height = 0UL;
    long status;

    if (!packet) {
        return;
    }

    window = gwes_find_window(packet->sender_pid, packet->arg0);
    if (!window) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS_REPLY, (unsigned long)ROS_USER_IPC_STATUS_NOT_FOUND, 0UL, 0UL, 0UL, NULL, NULL);
        return;
    }

    gwes_unpack_signed_pair(packet->arg1, &x, &y);
    gwes_unpack_pair(packet->arg2, &width, &height);
    if (width == 0UL || height == 0UL) {
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS_REPLY, (unsigned long)ROS_USER_IPC_STATUS_NOT_FOUND, 0UL, 0UL, 0UL, NULL, NULL);
        return;
    }

    if (gwes_window_is_decorated_root(window)) {
        previous_state = window->show_state;
        window->show_state = GWES_WINDOW_SHOW_NORMAL;
    }
    status = gwes_resize_window_record(window, x, y, width, height);
    if (status < 0L && gwes_window_is_decorated_root(window)) {
        window->show_state = previous_state;
    }
    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
}

/*
 * Record one client-selected cursor path for a live server window.
 *
 * GWES stores the raw asset path string and re-evaluates the visible cursor
 * immediately. That makes cursor changes observable even when the pointer is
 * already stationary above the window that just updated its policy.
 *
 * @param packet Client request that names the target window in `arg0`.
 * @return Nothing.
 */
static void gwes_handle_set_window_cursor(const UserIpcMessage* packet) {
    GwesWindowRecord* window;
    long status = ROS_USER_IPC_STATUS_OK;

    if (!packet) {
        return;
    }

    window = gwes_find_window(packet->sender_pid, packet->arg0);
    if (!window) {
        status = ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    else {
        gwes_copy_text(window->cursor_path, sizeof(window->cursor_path), packet->text);
        gwes_refresh_pointer_cursor();
    }

    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
}

/*
 * Apply one foreground activation request on the retained window model.
 *
 * Both the synchronous and asynchronous client APIs reuse this helper so the
 * focus, raise, cursor, and shell-state side effects stay identical.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
static long gwes_apply_foreground_window(unsigned long hwnd) {
    GwesWindowRecord* target;
    GwesWindowRecord* root;

    target = gwes_find_window_any(hwnd);
    root = gwes_find_root_window(target);
    if (root == NULL) {
        return ROS_USER_IPC_STATUS_NOT_FOUND;
    }

    gwes_set_focus(root->hwnd);
    (void)gwes_render_raise_window(root->hwnd);
    gwes_render_refresh_window_groups();
    gwes_refresh_pointer_cursor();
    gwes_publish_shell_state();
    return ROS_USER_IPC_STATUS_OK;
}

/*
 * Raise one existing top-level window and make it the foreground target.
 *
 * @param packet Client request that names the target window in `arg0`.
 * @return Nothing.
 */
static void gwes_handle_set_foreground_window(const UserIpcMessage* packet) {
    long status = ROS_USER_IPC_STATUS_OK;

    if (!packet) {
        return;
    }

    status = gwes_apply_foreground_window(packet->arg0);
    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
}

/*
 * Raise one existing top-level window without replying to the caller.
 *
 * Explorer uses this path when a taskbar gesture already completed locally and
 * only needs GWES to finish the focus transition on its own thread.
 *
 * @param packet Client request that names the target window in `arg0`.
 * @return Nothing.
 */
static void gwes_handle_post_foreground_window(const UserIpcMessage* packet) {
    if (!packet) {
        return;
    }

    (void)gwes_apply_foreground_window(packet->arg0);
}

/*
 * Register or update one per-window timer owned by the requesting client.
 *
 * @param packet Client timer request.
 * @return Nothing.
 */
static void gwes_handle_set_timer(const UserIpcMessage* packet) {
    GwesWindowRecord* window;
    GwesTimerRecord* timer;
    unsigned long timer_id;
    unsigned long interval_msec;
    unsigned long next_fire_msec;
    long status = ROS_USER_IPC_STATUS_OK;

    if (packet == NULL) {
        return;
    }

    window = gwes_find_window(packet->sender_pid, packet->arg0);
    interval_msec = packet->arg2;
    if (window == NULL || interval_msec == 0UL) {
        status = ROS_USER_IPC_STATUS_NOT_FOUND;
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_TIMER_REPLY, (unsigned long)status, packet->arg1, 0UL, 0UL, NULL, NULL);
        return;
    }

    timer_id = packet->arg1;
    if (timer_id == 0UL) {
        timer_id = g_gwes_next_timer_id++;
        if (timer_id == 0UL) {
            timer_id = g_gwes_next_timer_id++;
        }
    }

    timer = gwes_find_timer(packet->sender_pid, window->hwnd, timer_id);
    if (timer == NULL) {
        timer = gwes_allocate_timer();
        if (timer == NULL) {
            status = ROS_USER_IPC_STATUS_NO_SPACE;
            (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_TIMER_REPLY, (unsigned long)status, timer_id, 0UL, 0UL, NULL, NULL);
            return;
        }

        timer->in_use = 1;
        timer->owner_pid = packet->sender_pid;
        timer->hwnd = window->hwnd;
        timer->timer_id = timer_id;
    }

    next_fire_msec = getUptimeMs() + interval_msec;
    if (next_fire_msec == 0UL) {
        next_fire_msec = 1UL;
    }
    timer->interval_msec = interval_msec;
    timer->next_fire_msec = next_fire_msec;
    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_TIMER_REPLY, (unsigned long)status, timer_id, 0UL, 0UL, NULL, NULL);
}

/*
 * Cancel one per-window timer owned by the requesting client.
 *
 * @param packet Client timer-cancel request.
 * @return Nothing.
 */
static void gwes_handle_kill_timer(const UserIpcMessage* packet) {
    GwesWindowRecord* window;
    GwesTimerRecord* timer;
    long status = ROS_USER_IPC_STATUS_OK;

    if (packet == NULL) {
        return;
    }

    window = gwes_find_window(packet->sender_pid, packet->arg0);
    timer = window != NULL ? gwes_find_timer(packet->sender_pid, window->hwnd, packet->arg1) : NULL;
    if (timer == NULL) {
        status = ROS_USER_IPC_STATUS_NOT_FOUND;
    }
    else {
        gwes_release_timer_record(timer);
    }

    (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_KILL_TIMER_REPLY, (unsigned long)status, packet->arg1, 0UL, 0UL, NULL, NULL);
}

/*
 * Dispatch one incoming client packet by protocol kind.
 *
 * @param packet Client packet to handle.
 * @return Nothing.
 */
static void gwes_handle_packet(const UserIpcMessage* packet) {
    if (!packet || packet->protocol != ROS_WINDOW_SERVER_PROTOCOL) {
        return;
    }

    switch (packet->kind) {
    case ROS_WINDOW_SERVER_KIND_REGISTER_CLASS:
        gwes_handle_register_class(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_CREATE_WINDOW:
        gwes_handle_create_window(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_PROCESS_QUIT:
        gwes_handle_process_quit(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_INVALIDATE_WINDOW:
        gwes_handle_invalidate_window(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR:
        gwes_handle_set_window_cursor(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW:
        gwes_handle_set_foreground_window(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_POST_FOREGROUND_WINDOW:
        gwes_handle_post_foreground_window(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_SET_WINDOW_BOUNDS:
        gwes_handle_set_window_bounds(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_SET_TIMER:
        gwes_handle_set_timer(packet);
        break;
    case ROS_WINDOW_SERVER_KIND_KILL_TIMER:
        gwes_handle_kill_timer(packet);
        break;
    default:
        break;
    }
}

/*
 * Dispatch one queued client packet on the window/render owner thread.
 *
 * @param packet Queued client packet.
 * @return Nothing.
 */
void gwes_window_thread_handle_packet(const UserIpcMessage* packet) {
    gwes_handle_packet(packet);
}

/*
 * Deliver every per-window timer that is due at the supplied time.
 *
 * @param now Current uptime in milliseconds.
 * @return Count of delivered `WM_TIMER` messages.
 */
unsigned long gwes_window_thread_fire_due_timers(unsigned long now) {
    GwesTimerRecord* timer = g_gwes_timers;
    unsigned long delivered_count = 0UL;

    while (timer != NULL) {
        GwesTimerRecord* next = timer->next;
        GwesWindowRecord* window;

        if (!timer->in_use || timer->interval_msec == 0UL || timer->next_fire_msec > now) {
            timer = next;
            continue;
        }
        if (!gwes_pid_is_live(timer->owner_pid)) {
            GwesCleanupSummary summary = gwes_release_client_objects(timer->owner_pid);

            if (summary.released_classes != 0UL || summary.released_windows != 0UL) {
                gwes_log_cleanup(timer->owner_pid, 0UL, "timer-process-dead", summary);
            }
            timer = next;
            continue;
        }

        window = gwes_find_window(timer->owner_pid, timer->hwnd);
        if (window == NULL) {
            gwes_release_timer_record(timer);
            timer = next;
            continue;
        }

        do {
            timer->next_fire_msec += timer->interval_msec;
        } while (timer->next_fire_msec <= now);
        gwes_send_window_message(window, WM_TIMER, timer->timer_id, 0UL);
        ++delivered_count;
        timer = next;
    }

    return delivered_count;
}

/*
 * Compute the time until the next due window timer.
 *
 * @param now Current uptime in milliseconds.
 * @return Milliseconds until the next due timer, or one second when none exist.
 */
unsigned long gwes_window_thread_timer_wait_budget(unsigned long now) {
    GwesTimerRecord* timer;
    unsigned long best_wait = 1000UL;

    for (timer = g_gwes_timers; timer != NULL; timer = timer->next) {
        unsigned long wait_msec;

        if (!timer->in_use || timer->interval_msec == 0UL) {
            continue;
        }
        if (timer->next_fire_msec <= now) {
            return 0UL;
        }

        wait_msec = timer->next_fire_msec - now;
        if (wait_msec < best_wait) {
            best_wait = wait_msec;
        }
    }

    return best_wait;
}

int main(void) {
    long status;

    gwes_log_line("starting userspace window server");
    if (gwes_render_init() != 0) {
        gwes_log_line("render init failed; running without display output");
    }
    if (gwes_shell_state_acquire() < 0L) {
        gwes_log_line("shell state unavailable");
    }
    else {
        gwes_publish_shell_state();
    }
    {
        long shared_input_status = gwes_shared_input_acquire();

        if (shared_input_status < 0L) {
            char line[128];

            snprintf(line, sizeof(line), "gwes.exe: shared input acquire failed status=%ld", shared_input_status);
            writeLine(line);
        }
        else {
            gwes_log_line("shared input ready");
        }
    }
    status = gwes_workers_start();
    if (status < 0L) {
        gwes_log_line("worker startup failed");
        return 1;
    }

    gwes_service_thread_run();

    return 0;
}
