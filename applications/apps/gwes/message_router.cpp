#include "gwes_server.h"
#include "render.h"

#include <string.h>

/*
 * Declare the shared geometry snapshot helper explicitly for editor analysis.
 *
 * The build already sees the declaration through `gwes_server.h`, but keeping a
 * local forward declaration here avoids stale language-service confusion while
 * the modular rewrite settles.
 *
 * @param window Decorated top-level window whose restore geometry should be updated.
 * @return Nothing.
 */
void gwes_store_restore_geometry(GwesWindowRecord* window);

namespace {

    /*
     * Return whether GWES should post one message asynchronously rather than forcing inline handling.
     *
     * @param message Window message identifier.
     * @return Non-zero when the message belongs on the async queue.
     */
    int gwes_message_prefers_post(unsigned long message) {
        (void)message;
        return 1;
    }

    /*
     * Record one class registration request from a client and acknowledge it.
     *
     * @param packet Client request packet.
     * @return Nothing.
     */
    void gwes_handle_register_class(const UserIpcMessage* packet) {
        GwesClassRecord* record;

        if (packet == NULL) {
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
     * Create one server window record and send both the create reply and `WM_CREATE`.
     *
     * @param packet Client create-window request.
     * @return Nothing.
     */
    void gwes_handle_create_window(const UserIpcMessage* packet) {
        GwesClassRecord* class_record;
        GwesWindowRecord* window;
        unsigned long width;
        unsigned long height;
        long x;
        long y;
        long render_status;

        if (packet == NULL) {
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

        if (window->parent == 0UL
            && (window->style & ROS_WINDOW_STYLE_VISIBLE) != 0UL
            && (window->style & ROS_WINDOW_STYLE_SYSTEM_UI) == 0UL) {
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
    void gwes_handle_process_quit(const UserIpcMessage* packet) {
        GwesCleanupSummary summary;

        if (packet == NULL) {
            return;
        }

        summary = gwes_release_client_objects(packet->sender_pid);
        gwes_log_cleanup(packet->sender_pid, packet->arg0, "post-quit", summary);
    }

    /*
     * Mark one client-owned surface rectangle dirty after local GDI rendering.
     *
     * @param packet Client invalidate packet.
     * @return Nothing.
     */
    void gwes_handle_invalidate_window(const UserIpcMessage* packet) {
        GwesWindowRecord* window;
        unsigned long x;
        unsigned long y;
        unsigned long width;
        unsigned long height;

        if (packet == NULL) {
            return;
        }

        window = gwes_find_window(packet->sender_pid, packet->arg0);
        if (window == NULL) {
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
     * Consume one explicit move or resize request for a live client-owned window.
     *
     * @param packet Client request packet.
     * @return Nothing.
     */
    void gwes_handle_set_window_bounds(const UserIpcMessage* packet) {
        GwesWindowRecord* window;
        unsigned long previous_state = GWES_WINDOW_SHOW_NORMAL;
        long x = 0L;
        long y = 0L;
        unsigned long width = 0UL;
        unsigned long height = 0UL;
        long status;

        if (packet == NULL) {
            return;
        }

        window = gwes_find_window(packet->sender_pid, packet->arg0);
        if (window == NULL) {
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
     * @param packet Client request that names the target window in `arg0`.
     * @return Nothing.
     */
    void gwes_handle_set_window_cursor(const UserIpcMessage* packet) {
        GwesWindowRecord* window;
        long status = ROS_USER_IPC_STATUS_OK;

        if (packet == NULL) {
            return;
        }

        window = gwes_find_window(packet->sender_pid, packet->arg0);
        if (window == NULL) {
            status = ROS_USER_IPC_STATUS_NOT_FOUND;
        }
        else {
            gwes_copy_text(window->cursor_path, sizeof(window->cursor_path), packet->text);
            gwes_refresh_pointer_cursor();
        }

        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_WINDOW_CURSOR_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
    }

    /*
     * Raise one existing top-level window and make it the foreground target.
     *
     * @param packet Client request that names the target window in `arg0`.
     * @return Nothing.
     */
    void gwes_handle_set_foreground_window(const UserIpcMessage* packet) {
        long status = ROS_USER_IPC_STATUS_OK;

        if (packet == NULL) {
            return;
        }

        status = gwes_apply_foreground_window(packet->arg0);
        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_SET_FOREGROUND_WINDOW_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
    }

    /*
     * Raise one existing top-level window without replying to the caller.
     *
     * @param packet Client request that names the target window in `arg0`.
     * @return Nothing.
     */
    void gwes_handle_post_foreground_window(const UserIpcMessage* packet) {
        if (packet == NULL) {
            return;
        }

        (void)gwes_apply_foreground_window(packet->arg0);
    }

    /*
     * Destroy one live client-owned window and tear down its server render state.
     *
     * @param packet Client request that names the target window in `arg0`.
     * @return Nothing.
     */
    void gwes_handle_destroy_window(const UserIpcMessage* packet) {
        GwesWindowRecord* window;
        long status = ROS_USER_IPC_STATUS_OK;

        if (packet == NULL) {
            return;
        }

        window = gwes_find_window(packet->sender_pid, packet->arg0);
        if (window == NULL) {
            status = ROS_USER_IPC_STATUS_NOT_FOUND;
        }
        else {
            gwes_release_window_record(window, 1);
            gwes_render_refresh_window_groups();
            gwes_refresh_pointer_cursor();
            gwes_publish_shell_state();
        }

        (void)gwes_send_packet(packet->sender_pid, ROS_WINDOW_SERVER_KIND_DESTROY_WINDOW_REPLY, (unsigned long)status, 0UL, 0UL, 0UL, NULL, NULL);
    }

    /*
     * Register or update one per-window timer owned by the requesting client.
     *
     * @param packet Client timer request.
     * @return Nothing.
     */
    void gwes_handle_set_timer(const UserIpcMessage* packet) {
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
    void gwes_handle_kill_timer(const UserIpcMessage* packet) {
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
     * Start one authoritative popup-menu tracking session for the requesting client.
     *
     * @param packet Client track-popup request.
     * @return Nothing.
     */
    void gwes_handle_track_popup_menu(const UserIpcMessage* packet) {
        (void)gwes_track_popup_menu(packet);
    }

    /*
     * Dispatch one incoming client packet by protocol kind.
     *
     * @param packet Client packet to handle.
     * @return Nothing.
     */
    void gwes_handle_packet(const UserIpcMessage* packet) {
        if (packet == NULL || packet->protocol != ROS_WINDOW_SERVER_PROTOCOL) {
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
        case ROS_WINDOW_SERVER_KIND_DESTROY_WINDOW:
            gwes_handle_destroy_window(packet);
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
        case ROS_WINDOW_SERVER_KIND_TRACK_POPUP_MENU:
            gwes_handle_track_popup_menu(packet);
            break;
        default:
            break;
        }
    }

} // namespace

/*
 * Decode two signed 32-bit values packed into one IPC scalar.
 *
 * @param packed Packed pair encoded by the client-side window helper.
 * @param first Receives the upper signed 32-bit value.
 * @param second Receives the lower signed 32-bit value.
 * @return Nothing.
 */
void gwes_unpack_signed_pair(unsigned long packed, long* first, long* second) {
    if (first != NULL) {
        *first = (long)(int32_t)((packed >> 32) & 0xFFFFFFFFUL);
    }
    if (second != NULL) {
        *second = (long)(int32_t)(packed & 0xFFFFFFFFUL);
    }
}

/*
 * Decode two 32-bit values that were packed into one 64-bit IPC field.
 *
 * @param packed Packed pair encoded by the client-side drawing library.
 * @param first Receives the upper 32 bits.
 * @param second Receives the lower 32 bits.
 * @return Nothing.
 */
void gwes_unpack_pair(unsigned long packed, unsigned long* first, unsigned long* second) {
    if (first != NULL) {
        *first = (packed >> 32) & 0xFFFFFFFFUL;
    }
    if (second != NULL) {
        *second = packed & 0xFFFFFFFFUL;
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
long gwes_send_packet(long receiver_pid, unsigned long kind, unsigned long arg0, unsigned long arg1, unsigned long arg2, unsigned long arg3, const char* text, const char* text2) {
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
void gwes_send_window_message(GwesWindowRecord* window, unsigned long message, unsigned long wparam, unsigned long lparam) {
    if (window == NULL || !window->in_use) {
        return;
    }

    if (!gwes_message_prefers_post(message)) {
        return;
    }

    if (gwes_send_packet(window->owner_pid, ROS_WINDOW_SERVER_KIND_DELIVER_MESSAGE, window->hwnd, message, wparam, lparam, NULL, NULL) < 0) {
        GwesCleanupSummary summary = gwes_release_client_objects(window->owner_pid);

        if (summary.released_classes != 0UL || summary.released_windows != 0UL) {
            gwes_log_cleanup(window->owner_pid, 0UL, "deliver-failed", summary);
        }
    }
}

/*
 * Apply one foreground activation request on the retained window model.
 *
 * @param hwnd Target window handle.
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_apply_foreground_window(unsigned long hwnd) {
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
 * Dispatch one queued client packet on the window and render owner thread.
 *
 * @param packet Queued client packet.
 * @return Nothing.
 */
void gwes_window_thread_handle_packet(const UserIpcMessage* packet) {
    gwes_handle_packet(packet);
}