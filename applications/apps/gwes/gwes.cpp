#include "gwes_server.h"
#include "render.h"

#include <stdio.h>
#include <string.h>

GwesClassRecord* g_gwes_classes = NULL;
GwesWindowRecord* g_gwes_windows = NULL;
GwesTimerRecord* g_gwes_timers = NULL;
unsigned long g_gwes_class_count = 0UL;
unsigned long g_gwes_class_peak = 0UL;
unsigned long g_gwes_window_count = 0UL;
unsigned long g_gwes_window_peak = 0UL;
HWND g_gwes_next_hwnd = 1UL;
unsigned long g_gwes_next_timer_id = 1UL;
RosKernelGuiSharedInputRegion* g_gwes_shared_input = NULL;
unsigned long g_gwes_shared_input_consumer_index = 0UL;
int g_gwes_shared_input_attached = 0;
RosExplorerShellSharedState* g_gwes_shell_state = NULL;
HWND g_gwes_focus_hwnd = 0UL;
HWND g_gwes_last_task_focus_hwnd = 0UL;
HWND g_gwes_keyboard_capture_hwnd = 0UL;
HWND g_gwes_pointer_capture_hwnd = 0UL;
HWND g_gwes_pointer_down_hwnd = 0UL;
HWND g_gwes_right_pointer_down_hwnd = 0UL;
HWND g_gwes_hover_hwnd = 0UL;
HWND g_gwes_pointer_interaction_hwnd = 0UL;
unsigned long g_gwes_pointer_resize_edges = 0UL;
unsigned long g_gwes_pointer_x = 0UL;
unsigned long g_gwes_pointer_y = 0UL;
unsigned long g_gwes_pointer_buttons = 0UL;
unsigned long g_gwes_pointer_drag_start_x = 0UL;
unsigned long g_gwes_pointer_drag_start_y = 0UL;
long g_gwes_pointer_drag_origin_x = 0L;
long g_gwes_pointer_drag_origin_y = 0L;
unsigned long g_gwes_pointer_drag_origin_width = 0UL;
unsigned long g_gwes_pointer_drag_origin_height = 0UL;
long g_gwes_pointer_drag_offset_x = 0L;
long g_gwes_pointer_drag_offset_y = 0L;
int g_gwes_pointer_preview_active = 0;
long g_gwes_pointer_preview_x = 0L;
long g_gwes_pointer_preview_y = 0L;
unsigned long g_gwes_pointer_preview_width = 0UL;
unsigned long g_gwes_pointer_preview_height = 0UL;
int g_gwes_pointer_visible = 0;
int g_gwes_desktop_pointer_down = 0;
GwesMenuSession g_gwes_menu_session = { 0 };

/*
 * Copy one string into a fixed-size destination and always terminate it.
 *
 * @param destination Output buffer.
 * @param size Output buffer size.
 * @param text Source string.
 * @return Nothing.
 */
void gwes_copy_text(char* destination, unsigned long size, const char* text) {
    unsigned long index = 0UL;

    if (destination == NULL || size == 0UL) {
        return;
    }
    if (text == NULL) {
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
void gwes_log_line(const char* text) {
    char line[192];

    snprintf(line, sizeof(line), "gwes.exe: %s", text != NULL ? text : "");
    writeLine(line);
}

/*
 * Write one structured create-window failure line so client logs can be correlated.
 *
 * @param stage Short failure stage label.
 * @param packet Original client packet when available.
 * @param status Numeric failure code returned to the client.
 * @return Nothing.
 */
void gwes_log_create_failure(const char* stage, const UserIpcMessage* packet, long status) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: create failed stage=%s pid=%ld class=%s title=%s style=%lu size=%lux%lu status=%ld",
        stage != NULL ? stage : "unknown",
        packet != NULL ? packet->sender_pid : -1L,
        (packet != NULL && packet->text[0] != '\0') ? packet->text : "<null>",
        (packet != NULL && packet->text2[0] != '\0') ? packet->text2 : "",
        packet != NULL ? packet->arg3 : 0UL,
        packet != NULL ? ((packet->arg2 >> 32) & 0xFFFFFFFFUL) : 0UL,
        packet != NULL ? (packet->arg2 & 0xFFFFFFFFUL) : 0UL,
        status);
    writeLine(line);
}

/*
 * Write one compact client-request line for the low-frequency register/create paths.
 *
 * @param stage Short request label.
 * @param packet Original client packet.
 * @return Nothing.
 */
void gwes_log_client_request(const char* stage, const UserIpcMessage* packet) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: request stage=%s pid=%ld kind=%lu class=%s title=%s",
        stage != NULL ? stage : "unknown",
        packet != NULL ? packet->sender_pid : -1L,
        packet != NULL ? packet->kind : 0UL,
        (packet != NULL && packet->text[0] != '\0') ? packet->text : "<null>",
        (packet != NULL && packet->text2[0] != '\0') ? packet->text2 : "");
    writeLine(line);
}

/*
 * Write one structured cleanup line that explains why GWES released client-owned objects.
 *
 * @param owner_pid Client process identifier being cleaned up.
 * @param exit_code Application-provided quit code when available.
 * @param reason Short reason string for the cleanup path.
 * @param summary Counts of released objects.
 * @return Nothing.
 */
void gwes_log_cleanup(long owner_pid, unsigned long exit_code, const char* reason, GwesCleanupSummary summary) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: cleanup pid=%ld exit=%lu reason=%s classes=%lu windows=%lu",
        owner_pid,
        exit_code,
        reason != NULL ? reason : "unknown",
        summary.released_classes,
        summary.released_windows);
    writeLine(line);
}

/*
 * Emit one compact registry occupancy line for scaling diagnostics.
 *
 * @param reason Short label describing why the snapshot is being emitted.
 * @return Nothing.
 */
void gwes_log_registry_counts(const char* reason) {
    char line[192];

    snprintf(
        line,
        sizeof(line),
        "gwes.exe: registry reason=%s classes=%lu peak=%lu windows=%lu peak=%lu",
        reason != NULL ? reason : "unknown",
        g_gwes_class_count,
        g_gwes_class_peak,
        g_gwes_window_count,
        g_gwes_window_peak);
    writeLine(line);
}

/*
 * Record one class-count increase and emit a snapshot when the high-water mark changes.
 *
 * @return Nothing.
 */
void gwes_note_class_alloc(void) {
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
void gwes_note_class_release(void) {
    if (g_gwes_class_count != 0UL) {
        --g_gwes_class_count;
    }

    gwes_log_registry_counts("class-release");
}

/*
 * Record one window-count increase and emit a snapshot when the high-water mark changes.
 *
 * @return Nothing.
 */
void gwes_note_window_alloc(void) {
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
void gwes_note_window_release(void) {
    if (g_gwes_window_count != 0UL) {
        --g_gwes_window_count;
    }

    gwes_log_registry_counts("window-release");
}

/*
 * Cache and draw one interactive move or resize preview rectangle.
 *
 * @param x Preview outer-frame X coordinate.
 * @param y Preview outer-frame Y coordinate.
 * @param width Preview outer-frame width.
 * @param height Preview outer-frame height.
 * @return Nothing.
 */
void gwes_set_pointer_interaction_preview(long x, long y, unsigned long width, unsigned long height) {
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
void gwes_shared_input_barrier(void) {
    asm volatile("dmb ishld" ::: "memory");
}

/*
 * Report whether one PID still resolves to a live task.
 *
 * @param pid Process identifier to query.
 * @return Non-zero when the PID is live.
 */
int gwes_pid_is_live(long pid) {
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
void gwes_send_shell_ipc(unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3) {
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
 * Start the userspace window server bootstrap and then enter the broker loop.
 *
 * @return Zero on success, or a process exit code on fatal startup failure.
 */
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