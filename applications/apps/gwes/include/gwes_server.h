#ifndef ROS_APP_GWES_SERVER_H
#define ROS_APP_GWES_SERVER_H

#define ROS_WINDOW_SERVER_ONLY 1
#define ROS_WINDOW_NO_IMPORTS 1
#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_EXPLORER_SHELL 1

#include "app/app.h"
#include "gwes_workers.h"

#include <stdint.h>

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

typedef struct GwesMenuPopup {
    HWND hwnd;
    unsigned long menu_index;
    unsigned long hover_index;
    unsigned long popup_width;
    unsigned long popup_height;
} GwesMenuPopup;

typedef struct GwesMenuSession {
    int active;
    long owner_pid;
    HWND owner_hwnd;
    unsigned long flags;
    unsigned long open_popup_count;
    int submenu_open_armed;
    unsigned long submenu_open_due_msec;
    unsigned long submenu_open_popup_slot;
    unsigned long submenu_open_item_index;
    int submenu_open_focus_first_item;
    int submenu_close_armed;
    unsigned long submenu_close_due_msec;
    unsigned long submenu_close_first_slot;
    RosWindowMenuModel model;
    GwesMenuPopup popups[ROS_WINDOW_MENU_MAX_MENUS];
} GwesMenuSession;

extern GwesClassRecord* g_gwes_classes;
extern GwesWindowRecord* g_gwes_windows;
extern GwesTimerRecord* g_gwes_timers;
extern unsigned long g_gwes_class_count;
extern unsigned long g_gwes_class_peak;
extern unsigned long g_gwes_window_count;
extern unsigned long g_gwes_window_peak;
extern HWND g_gwes_next_hwnd;
extern unsigned long g_gwes_next_timer_id;
extern RosKernelGuiSharedInputRegion* g_gwes_shared_input;
extern unsigned long g_gwes_shared_input_consumer_index;
extern int g_gwes_shared_input_attached;
extern RosExplorerShellSharedState* g_gwes_shell_state;
extern HWND g_gwes_focus_hwnd;
extern HWND g_gwes_last_task_focus_hwnd;
extern HWND g_gwes_keyboard_capture_hwnd;
extern HWND g_gwes_pointer_capture_hwnd;
extern HWND g_gwes_pointer_down_hwnd;
extern HWND g_gwes_right_pointer_down_hwnd;
extern HWND g_gwes_hover_hwnd;
extern HWND g_gwes_pointer_interaction_hwnd;
extern unsigned long g_gwes_pointer_resize_edges;
extern unsigned long g_gwes_pointer_x;
extern unsigned long g_gwes_pointer_y;
extern unsigned long g_gwes_pointer_buttons;
extern unsigned long g_gwes_pointer_drag_start_x;
extern unsigned long g_gwes_pointer_drag_start_y;
extern long g_gwes_pointer_drag_origin_x;
extern long g_gwes_pointer_drag_origin_y;
extern unsigned long g_gwes_pointer_drag_origin_width;
extern unsigned long g_gwes_pointer_drag_origin_height;
extern long g_gwes_pointer_drag_offset_x;
extern long g_gwes_pointer_drag_offset_y;
extern int g_gwes_pointer_preview_active;
extern long g_gwes_pointer_preview_x;
extern long g_gwes_pointer_preview_y;
extern unsigned long g_gwes_pointer_preview_width;
extern unsigned long g_gwes_pointer_preview_height;
extern int g_gwes_pointer_visible;
extern int g_gwes_desktop_pointer_down;
extern GwesMenuSession g_gwes_menu_session;

/*
 * Pack two signed coordinates into the Win32-style payload format used by the
 * client runtime and server delivery path.
 *
 * @param first Upper signed coordinate.
 * @param second Lower signed coordinate.
 * @return Packed message payload.
 */
static inline unsigned long gwes_pack_signed_pair(long first, long second) {
    return WindowPackSignedPair(first, second);
}

/*
 * Copy one short string into a fixed-size destination and always terminate it.
 *
 * @param destination Output buffer.
 * @param size Output buffer size.
 * @param text Source string.
 * @return Nothing.
 */
void gwes_copy_text(char* destination, unsigned long size, const char* text);

/*
 * Write one tagged GWES log line to the userspace console.
 *
 * @param text Message body.
 * @return Nothing.
 */
void gwes_log_line(const char* text);

/*
 * Write one structured create-window failure line for diagnostics.
 *
 * @param stage Short failure label.
 * @param packet Original client request when available.
 * @param status Numeric failure status.
 * @return Nothing.
 */
void gwes_log_create_failure(const char* stage, const UserIpcMessage* packet, long status);

/*
 * Write one compact line for low-frequency client handshake requests.
 *
 * @param stage Short request label.
 * @param packet Original client packet.
 * @return Nothing.
 */
void gwes_log_client_request(const char* stage, const UserIpcMessage* packet);

/*
 * Write one structured cleanup line when GWES releases client-owned state.
 *
 * @param owner_pid Client process identifier.
 * @param exit_code Exit code when available.
 * @param reason Cleanup reason.
 * @param summary Released object counts.
 * @return Nothing.
 */
void gwes_log_cleanup(long owner_pid, unsigned long exit_code, const char* reason, GwesCleanupSummary summary);

/*
 * Emit one registry occupancy snapshot for capacity diagnostics.
 *
 * @param reason Short reason label.
 * @return Nothing.
 */
void gwes_log_registry_counts(const char* reason);

/*
 * Record one class allocation in the running occupancy counters.
 *
 * @return Nothing.
 */
void gwes_note_class_alloc(void);

/*
 * Record one class release in the running occupancy counters.
 *
 * @return Nothing.
 */
void gwes_note_class_release(void);

/*
 * Record one window allocation in the running occupancy counters.
 *
 * @return Nothing.
 */
void gwes_note_window_alloc(void);

/*
 * Record one window release in the running occupancy counters.
 *
 * @return Nothing.
 */
void gwes_note_window_release(void);

/*
 * Cache and draw the current interactive move or resize placeholder rectangle.
 *
 * @param x Preview outer-frame X coordinate.
 * @param y Preview outer-frame Y coordinate.
 * @param width Preview outer-frame width.
 * @param height Preview outer-frame height.
 * @return Nothing.
 */
void gwes_set_pointer_interaction_preview(long x, long y, unsigned long width, unsigned long height);

/*
 * Order one load barrier before consuming kernel-published shared-input state.
 *
 * @return Nothing.
 */
void gwes_shared_input_barrier(void);

/*
 * Report whether one task identifier still resolves to a live process.
 *
 * @param pid Process identifier to query.
 * @return Non-zero when the task is still running.
 */
int gwes_pid_is_live(long pid);

/*
 * Decode two packed unsigned 32-bit values from one IPC word.
 *
 * @param packed Packed scalar.
 * @param first Receives the upper value.
 * @param second Receives the lower value.
 * @return Nothing.
 */
void gwes_unpack_pair(unsigned long packed, unsigned long* first, unsigned long* second);

/*
 * Decode two packed signed 32-bit values from one IPC word.
 *
 * @param packed Packed scalar.
 * @param first Receives the upper signed value.
 * @param second Receives the lower signed value.
 * @return Nothing.
 */
void gwes_unpack_signed_pair(unsigned long packed, long* first, long* second);

/*
 * Send one explorer shell-control IPC packet to the published shell process.
 *
 * @param kind Explorer IPC kind.
 * @param arg0 First scalar payload.
 * @param arg1 Second scalar payload.
 * @param arg2 Third scalar payload.
 * @param arg3 Fourth scalar payload.
 * @return Nothing.
 */
void gwes_send_shell_ipc(unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3);

/*
 * Map the shared explorer shell state block used to publish task metadata.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_shell_state_acquire(void);

/*
 * Find one class registration owned by the supplied client process.
 *
 * @param owner_pid Client process identifier.
 * @param class_name Registered class name.
 * @return Matching class record, or NULL when none exists.
 */
GwesClassRecord* gwes_find_class(long owner_pid, const char* class_name);

/*
 * Allocate one heap-backed class record and link it into the live registry.
 *
 * @return New class record, or NULL when allocation fails.
 */
GwesClassRecord* gwes_allocate_class(void);

/*
 * Release one class record from the live registry.
 *
 * @param record Class record to unlink.
 * @return Nothing.
 */
void gwes_release_class_record(GwesClassRecord* record);

/*
 * Allocate one heap-backed server window record.
 *
 * @return New window record, or NULL when allocation fails.
 */
GwesWindowRecord* gwes_allocate_window(void);

/*
 * Release one server window record and optionally tear down its render state.
 *
 * @param window Window record to release.
 * @param destroy_render_state Non-zero when compositor state should be removed.
 * @return Nothing.
 */
void gwes_release_window_record(GwesWindowRecord* window, int destroy_render_state);

/*
 * Find one timer owned by the supplied client and window.
 *
 * @param owner_pid Client process identifier.
 * @param hwnd Target window handle.
 * @param timer_id Timer identifier.
 * @return Matching timer record, or NULL when none exists.
 */
GwesTimerRecord* gwes_find_timer(long owner_pid, unsigned long hwnd, unsigned long timer_id);

/*
 * Allocate one heap-backed timer record and link it into the live registry.
 *
 * @return New timer record, or NULL when allocation fails.
 */
GwesTimerRecord* gwes_allocate_timer(void);

/*
 * Release one timer record from the live registry.
 *
 * @param timer Timer record to release.
 * @return Nothing.
 */
void gwes_release_timer_record(GwesTimerRecord* timer);

/*
 * Release every timer currently owned by one window.
 *
 * @param hwnd Target window handle.
 * @return Nothing.
 */
void gwes_release_window_timers(unsigned long hwnd);

/*
 * Find one live window owned by the supplied client process.
 *
 * @param owner_pid Owning client process identifier.
 * @param hwnd Stable window handle.
 * @return Matching window record, or NULL when none exists.
 */
GwesWindowRecord* gwes_find_window(long owner_pid, unsigned long hwnd);

/*
 * Find one live window by handle regardless of owner process.
 *
 * @param hwnd Stable window handle.
 * @return Matching window record, or NULL when none exists.
 */
GwesWindowRecord* gwes_find_window_any(unsigned long hwnd);

/*
 * Walk parent pointers until one top-level ancestor is reached.
 *
 * @param window Starting child or top-level window.
 * @return Top-level ancestor, or the original window when it has no parent.
 */
GwesWindowRecord* gwes_find_root_window(GwesWindowRecord* window);

/*
 * Drop focus and capture bookkeeping when one window disappears.
 *
 * @param hwnd Window being released.
 * @return Nothing.
 */
void gwes_forget_window_state(unsigned long hwnd);

/*
 * Record one new focus target and republish shell-visible state.
 *
 * @param hwnd Focus target.
 * @return Nothing.
 */
void gwes_set_focus(unsigned long hwnd);

/*
 * Publish one fresh task and work-area snapshot into explorer shared state.
 *
 * @return Nothing.
 */
void gwes_publish_shell_state(void);

/*
 * Snapshot one decorated top-level window's restorable geometry.
 *
 * @param window Target window record.
 * @return Nothing.
 */
void gwes_store_restore_geometry(GwesWindowRecord* window);

/*
 * Report whether one server window is a decorated top-level dialog.
 *
 * @param window Candidate server window.
 * @return Non-zero when GWES chrome applies.
 */
int gwes_window_is_decorated_root(const GwesWindowRecord* window);

/*
 * Report whether one server window is Explorer's non-topmost desktop surface.
 *
 * @param window Candidate server window.
 * @return Non-zero when the window is the desktop background.
 */
int gwes_window_is_desktop_background(const GwesWindowRecord* window);

/*
 * Filter one pointer-move target before hover and message delivery update.
 *
 * @param target Hit-tested window under the pointer.
 * @return Original target, or NULL when motion should take the desktop fast path.
 */
GwesWindowRecord* gwes_filter_pointer_move_target(GwesWindowRecord* target);

/*
 * Report whether one point hits the close button on a decorated top-level window.
 *
 * @param window Decorated root window.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the close button was hit.
 */
int gwes_window_hit_close_button(const GwesWindowRecord* window, unsigned long x, unsigned long y);

/*
 * Report whether one point hits the maximize button on a decorated root window.
 *
 * @param window Decorated root window.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the maximize button was hit.
 */
int gwes_window_hit_maximize_button(const GwesWindowRecord* window, unsigned long x, unsigned long y);

/*
 * Report whether one point hits the minimize button on a decorated root window.
 *
 * @param window Decorated root window.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the minimize button was hit.
 */
int gwes_window_hit_minimize_button(const GwesWindowRecord* window, unsigned long x, unsigned long y);

/*
 * Report whether one point hits the title bar but not any caption buttons.
 *
 * @param window Decorated root window.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Non-zero when the title bar should begin a move drag.
 */
int gwes_window_hit_title_bar(const GwesWindowRecord* window, unsigned long x, unsigned long y);

/*
 * Report which resize edges should react for one point on a decorated root window.
 *
 * @param window Decorated root window.
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @return Resize-edge mask.
 */
unsigned long gwes_window_resize_edges(const GwesWindowRecord* window, unsigned long x, unsigned long y);

/*
 * Clear the current pointer drag or resize session.
 *
 * @return Nothing.
 */
void gwes_clear_pointer_interaction(void);

/*
 * Start one title-bar drag or border-resize interaction.
 *
 * @param window Target top-level window.
 * @param pointer_x Desktop X coordinate at interaction start.
 * @param pointer_y Desktop Y coordinate at interaction start.
 * @param resize_edges Non-zero for resize interactions, or zero for moves.
 * @return Nothing.
 */
void gwes_begin_pointer_interaction(GwesWindowRecord* window, unsigned long pointer_x, unsigned long pointer_y, unsigned long resize_edges);

/*
 * Deliver synthetic hover-leave state when the hovered target changes.
 *
 * @param next_hwnd Window now under the pointer, or zero when none is hovered.
 * @param buttons Current button mask.
 * @param x Current desktop X coordinate.
 * @param y Current desktop Y coordinate.
 * @return Nothing.
 */
void gwes_update_hover(unsigned long next_hwnd, unsigned long buttons, unsigned long x, unsigned long y);

/*
 * Move one window through the retained renderer and mirror `WM_MOVE`.
 *
 * @param window Target window.
 * @param x Requested outer-frame X coordinate.
 * @param y Requested outer-frame Y coordinate.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_move_window_record(GwesWindowRecord* window, long x, long y);

/*
 * Resize one window through the retained renderer and mirror size notifications.
 *
 * @param window Target window.
 * @param x Requested outer-frame X coordinate.
 * @param y Requested outer-frame Y coordinate.
 * @param width Requested outer-frame width.
 * @param height Requested outer-frame height.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_resize_window_record(GwesWindowRecord* window, long x, long y, unsigned long width, unsigned long height);

/*
 * Commit the current placeholder move or resize back into live window state.
 *
 * @return Nothing.
 */
void gwes_commit_pointer_interaction(void);

/*
 * Toggle one decorated root window between normal and maximized geometry.
 *
 * @param window Decorated root window.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_toggle_maximize_window(GwesWindowRecord* window);

/*
 * Toggle one decorated root window between restorable and minimized geometry.
 *
 * @param window Decorated root window.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_toggle_minimize_window(GwesWindowRecord* window);

/*
 * Query the current desktop work area after shell reservations are applied.
 *
 * @param x Receives the work-area origin X.
 * @param y Receives the work-area origin Y.
 * @param width Receives the usable desktop width.
 * @param height Receives the usable desktop height.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_query_shell_work_area(long* x, long* y, unsigned long* width, unsigned long* height);

/*
 * Release every class and window currently owned by one client process.
 *
 * @param owner_pid Client process identifier.
 * @return Counts of released objects.
 */
GwesCleanupSummary gwes_release_client_objects(long owner_pid);

/*
 * Reap state left behind by client processes that died without quitting cleanly.
 *
 * @return Nothing.
 */
void gwes_reap_dead_clients(void);

/*
 * Resolve the current pointer target, honoring capture before hit testing.
 *
 * @param x Desktop X coordinate.
 * @param y Desktop Y coordinate.
 * @param local_x Receives window-relative X when a target exists.
 * @param local_y Receives window-relative Y when a target exists.
 * @return Target window record, or NULL when nothing was hit.
 */
GwesWindowRecord* gwes_resolve_pointer_target(unsigned long x, unsigned long y, long* local_x, long* local_y);

/*
 * Resolve the effective cursor path for one window subtree.
 *
 * @param window Window under the pointer, or NULL when nothing is hovered.
 * @return DOS-style cursor asset path.
 */
const char* gwes_effective_cursor_path(GwesWindowRecord* window);

/*
 * Push the current pointer state and resolved cursor into the compositor.
 *
 * @return Nothing.
 */
void gwes_refresh_pointer_cursor(void);

/*
 * Push one already-resolved cursor target into the compositor.
 *
 * @param target Window currently under the pointer, or NULL for the desktop.
 * @return Nothing.
 */
void gwes_refresh_pointer_cursor_for_target(GwesWindowRecord* target);

/*
 * Adopt the kernel's last published pointer snapshot after shared-input attach.
 *
 * @param pointer_state Kernel-owned pointer snapshot.
 * @return Nothing.
 */
void gwes_sync_pointer_snapshot(const RosKernelGuiPointerState* pointer_state);

/*
 * Report whether GWES currently owns one active authoritative popup-menu session.
 *
 * @return Non-zero when one popup menu is active.
 */
int gwes_menu_is_active(void);

/*
 * Start one authoritative popup-menu tracking session on behalf of one client.
 *
 * The function replies immediately on setup failure and otherwise defers the
 * reply until the popup is dismissed or one command is chosen.
 *
 * @param packet Client request packet.
 * @return Zero when setup succeeded, or a negative status code on failure.
 */
long gwes_track_popup_menu(const UserIpcMessage* packet);

/*
 * Consume one keyboard event while the authoritative popup menu is active.
 *
 * @param event Shared-input keyboard event.
 * @return Non-zero when the event was consumed by the menu session.
 */
int gwes_menu_dispatch_keyboard_event(const RosKernelGuiInputEvent* event);

/*
 * Consume one pointer event while the authoritative popup menu is active.
 *
 * @param event Shared-input pointer event.
 * @return Non-zero when the event was consumed by the menu session.
 */
int gwes_menu_dispatch_pointer_event(const RosKernelGuiInputEvent* event);

/*
 * Acquire the fixed shared-input mapping published by the kernel GUI service.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_shared_input_acquire(void);

/*
 * Release the kernel shared-input mapping during shutdown or reattach.
 *
 * @return Nothing.
 */
void gwes_shared_input_release(void);

/*
 * Deliver one reply or window message packet to a client process.
 *
 * @param receiver_pid Destination client PID.
 * @param kind GWES protocol kind.
 * @param arg0 First scalar field.
 * @param arg1 Second scalar field.
 * @param arg2 Third scalar field.
 * @param arg3 Fourth scalar field.
 * @param text Optional primary text field.
 * @param text2 Optional secondary text field.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_send_packet(long receiver_pid, unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3, const char* text, const char* text2);

/*
 * Mirror one window message back to its owning client process.
 *
 * @param window Target server window.
 * @param message Window message identifier.
 * @param wparam First payload word.
 * @param lparam Second payload word.
 * @return Nothing.
 */
void gwes_send_window_message(GwesWindowRecord* window, unsigned long message, unsigned long wparam, unsigned long lparam);

/*
 * Apply one foreground activation request to the retained window model.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_apply_foreground_window(unsigned long hwnd);

#endif