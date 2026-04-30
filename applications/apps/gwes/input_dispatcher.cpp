#include "gwes_server.h"
#include "render.h"

#include <stdint.h>
#include <string.h>

/*
 * Resolve the window that should currently receive pointer input.
 *
 * @param x Desktop pointer X coordinate.
 * @param y Desktop pointer Y coordinate.
 * @param local_x Receives the client-relative X coordinate when a target exists.
 * @param local_y Receives the client-relative Y coordinate when a target exists.
 * @return Target server window record, or NULL when no visible window was hit.
 */
GwesWindowRecord* gwes_resolve_pointer_target(unsigned long x, unsigned long y, long* local_x, long* local_y) {
	GwesWindowRecord* target = NULL;
	unsigned long target_hwnd = 0UL;
	long resolved_local_x = 0L;
	long resolved_local_y = 0L;

	if (local_x != NULL) {
		*local_x = 0L;
	}
	if (local_y != NULL) {
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
		if (local_x != NULL) {
			*local_x = resolved_local_x;
		}
		if (local_y != NULL) {
			*local_y = resolved_local_y;
		}
	}

	return target;
}

/*
 * Resolve the effective cursor asset for one window subtree.
 *
 * @param window Window under the pointer, or NULL when nothing is hovered.
 * @return DOS-style `.cur32` path that should drive the visible cursor.
 */
const char* gwes_effective_cursor_path(GwesWindowRecord* window) {
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
 * Push one already-resolved cursor target into the compositor.
 *
 * @param target Window currently under the pointer, or NULL for the desktop.
 * @return Nothing.
 */
void gwes_refresh_pointer_cursor_for_target(GwesWindowRecord* target) {
	const char* cursor_path = ROS_WINDOW_CURSOR_DEFAULT_PATH;

	if (g_gwes_pointer_visible) {
		cursor_path = gwes_effective_cursor_path(target);
	}

	gwes_render_update_pointer(g_gwes_pointer_x, g_gwes_pointer_y, g_gwes_pointer_visible, cursor_path);
}

/*
 * Push the current GWES pointer state into the compositor.
 *
 * @return Nothing.
 */
void gwes_refresh_pointer_cursor(void) {
	GwesWindowRecord* target = NULL;

	if (g_gwes_pointer_visible) {
		target = gwes_resolve_pointer_target(g_gwes_pointer_x, g_gwes_pointer_y, NULL, NULL);
	}

	gwes_refresh_pointer_cursor_for_target(target);
}

/*
 * Adopt the kernel's last published pointer snapshot after GWES attaches.
 *
 * @param pointer_state Kernel-owned pointer snapshot.
 * @return Nothing.
 */
void gwes_sync_pointer_snapshot(const RosKernelGuiPointerState* pointer_state) {
	if (pointer_state == NULL) {
		return;
	}

	g_gwes_pointer_x = pointer_state->x;
	g_gwes_pointer_y = pointer_state->y;
	g_gwes_pointer_buttons = pointer_state->pressed != 0U ? ROS_KERNEL_GUI_POINTER_BUTTON_LEFT : 0U;
	g_gwes_pointer_visible = pointer_state->visible != 0U;
	gwes_refresh_pointer_cursor();
}

/*
 * Return the newly pressed pointer-button bits for one shared-input event.
 *
 * GWES receives the full post-event button mask, so it compares that snapshot
 * with its last delivered mask to recover which logical button changed.
 *
 * @param previous_buttons Button mask before the current event.
 * @param current_buttons Button mask carried by the current event.
 * @return Bitmask of buttons that transitioned from up to down.
 */
static unsigned long gwes_pressed_buttons(unsigned long previous_buttons, unsigned long current_buttons) {
	return current_buttons & ~previous_buttons;
}

/*
 * Return the newly released pointer-button bits for one shared-input event.
 *
 * GWES uses the same full-mask comparison for release events so clients can
 * distinguish primary and secondary button ups without a new kernel ABI.
 *
 * @param previous_buttons Button mask before the current event.
 * @param current_buttons Button mask carried by the current event.
 * @return Bitmask of buttons that transitioned from down to up.
 */
static unsigned long gwes_released_buttons(unsigned long previous_buttons, unsigned long current_buttons) {
	return previous_buttons & ~current_buttons;
}

/*
 * Map one logical button transition to the corresponding window message.
 *
 * The current desktop feature only needs left and right buttons, so GWES
 * treats any non-secondary transition as the existing primary-button message.
 *
 * @param event_type Pointer-down or pointer-up shared-input event type.
 * @param changed_buttons Bitmask describing the button transition.
 * @return Window message identifier to deliver.
 */
static unsigned long gwes_pointer_button_message(uint32_t event_type, unsigned long changed_buttons) {
	const int secondary_button = (changed_buttons & ROS_KERNEL_GUI_POINTER_BUTTON_RIGHT) != 0UL;

	if (event_type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN) {
		return secondary_button ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
	}

	return secondary_button ? WM_RBUTTONUP : WM_LBUTTONUP;
}

/*
 * Acquire the fixed shared-input view published by the kernel GUI service.
 *
 * @return Zero on success, or a negative status code on failure.
 */
long gwes_shared_input_acquire(void) {
	RosKernelGuiSharedInputView view;
	long status;

	memset(&view, 0, sizeof(view));
	status = controlGui(ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE, (unsigned long)&view);
	if (status < 0) {
		return status;
	}
	if (view.version != ROS_KERNEL_GUI_SHARED_INPUT_VERSION
		|| view.view_address == 0ULL
		|| view.view_size < ros_kernel_gui_shared_input_min_bytes()) {
		return ROS_USER_IPC_STATUS_NOT_FOUND;
	}

	g_gwes_shared_input = (RosKernelGuiSharedInputRegion*)(uintptr_t)view.view_address;
	if (g_gwes_shared_input->magic != ROS_KERNEL_GUI_SHARED_INPUT_MAGIC
		|| g_gwes_shared_input->version != ROS_KERNEL_GUI_SHARED_INPUT_VERSION
		|| g_gwes_shared_input->capacity != ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY
		|| g_gwes_shared_input->record_size != sizeof(RosKernelGuiSharedInputRecord)
		|| g_gwes_shared_input->consumer_size != sizeof(RosKernelGuiSharedInputConsumer)
		|| view.consumer_index >= g_gwes_shared_input->max_consumers) {
		g_gwes_shared_input = NULL;
		return ROS_USER_IPC_STATUS_NOT_FOUND;
	}

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
void gwes_shared_input_release(void) {
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

	if (event == NULL) {
		return;
	}
	if (gwes_menu_is_active() && gwes_menu_dispatch_keyboard_event(event)) {
		return;
	}
	if (target_hwnd == 0UL) {
		return;
	}

	target = gwes_find_window_any(target_hwnd);
	if (target == NULL) {
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
	unsigned long previous_buttons = g_gwes_pointer_buttons;
	unsigned long changed_buttons = 0UL;
	long local_x = 0L;
	long local_y = 0L;
	long pressed_local_x = 0L;
	long pressed_local_y = 0L;
	unsigned long message;

	if (event == NULL) {
		return;
	}

	g_gwes_pointer_x = event->x;
	g_gwes_pointer_y = event->y;

	if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE) {
		g_gwes_pointer_visible = 0;
		g_gwes_pointer_buttons = 0UL;
		g_gwes_pointer_down_hwnd = 0UL;
		g_gwes_right_pointer_down_hwnd = 0UL;
		g_gwes_desktop_pointer_down = 0;
		gwes_update_hover(0UL, event->buttons, event->x, event->y);
		gwes_clear_pointer_interaction();
		gwes_refresh_pointer_cursor();
		if (gwes_menu_is_active()) {
			(void)gwes_menu_dispatch_pointer_event(event);
		}
		return;
	}

	g_gwes_pointer_visible = 1;
	if (gwes_menu_is_active() && gwes_menu_dispatch_pointer_event(event)) {
		g_gwes_pointer_buttons = event->buttons;
		return;
	}

	if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE && g_gwes_pointer_interaction_hwnd != 0UL) {
		target = gwes_find_window_any(g_gwes_pointer_interaction_hwnd);
		gwes_refresh_pointer_cursor_for_target(target);
		if (target == NULL) {
			g_gwes_pointer_buttons = event->buttons;
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
		g_gwes_pointer_buttons = event->buttons;
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
			g_gwes_pointer_buttons = event->buttons;
			return;
		}
		message = WM_MOUSEMOVE;
		break;
	case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN:
		changed_buttons = gwes_pressed_buttons(previous_buttons, event->buttons);
		if (changed_buttons == 0UL) {
			changed_buttons = event->buttons;
		}
		gwes_update_hover(target != NULL ? target->hwnd : 0UL, event->buttons, event->x, event->y);
		if (target == NULL) {
			g_gwes_desktop_pointer_down = 1;
			g_gwes_pointer_buttons = event->buttons;
			gwes_send_shell_ipc(
				ROS_EXPLORER_SHELL_IPC_KIND_DESKTOP_CLICK,
				event->x,
				event->y,
				event->buttons,
				0UL);
			return;
		}
		g_gwes_desktop_pointer_down = 0;
		message = gwes_pointer_button_message(event->type, changed_buttons);
		break;
	case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP:
		changed_buttons = gwes_released_buttons(previous_buttons, event->buttons);
		if (changed_buttons == 0UL) {
			changed_buttons = previous_buttons;
		}
		gwes_update_hover(target != NULL ? target->hwnd : 0UL, event->buttons, event->x, event->y);
		if (target == NULL) {
			g_gwes_desktop_pointer_down = 0;
		}
		message = gwes_pointer_button_message(event->type, changed_buttons);
		break;
	default:
		g_gwes_pointer_buttons = event->buttons;
		return;
	}

	if (message == WM_LBUTTONDOWN) {
		root = gwes_find_root_window(target);
		previous_focus_root = gwes_find_root_window(gwes_find_window_any(g_gwes_focus_hwnd));
		const int activate_target = (root != NULL) && !gwes_window_is_desktop_background(root);

		g_gwes_pointer_down_hwnd = target->hwnd;
		if (activate_target) {
			gwes_set_focus(target->hwnd);
		}
		if (activate_target && (previous_focus_root == NULL || previous_focus_root->hwnd != root->hwnd)) {
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
	else if (message == WM_RBUTTONDOWN) {
		g_gwes_right_pointer_down_hwnd = target->hwnd;
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
	else if (message == WM_RBUTTONUP) {
		pressed_target = gwes_find_window_any(g_gwes_right_pointer_down_hwnd);
		if (pressed_target != NULL && (target == NULL || pressed_target->hwnd != target->hwnd)) {
			if (gwes_render_translate_pointer(pressed_target->hwnd, event->x, event->y, &pressed_local_x, &pressed_local_y) != 0L) {
				pressed_local_x = 0L;
				pressed_local_y = 0L;
			}
			gwes_send_window_message(pressed_target, WM_RBUTTONUP, event->buttons, gwes_pack_signed_pair(pressed_local_x, pressed_local_y));
		}
		g_gwes_right_pointer_down_hwnd = 0UL;
	}

	g_gwes_pointer_buttons = event->buttons;
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
	consumer = ros_kernel_gui_shared_input_consumer_at_volatile(region, (uint32_t)g_gwes_shared_input_consumer_index);
	now = getUptimeMs();

	gwes_shared_input_barrier();
	tail_sequence = region->tail_sequence;
	head_sequence = consumer->head_sequence;
	if (head_sequence + region->capacity < tail_sequence) {
		consumer->drop_count += (tail_sequence - region->capacity) - head_sequence;
		head_sequence = tail_sequence - region->capacity;
	}

	while (head_sequence < tail_sequence) {
		RosKernelGuiInputEvent event;

		shared_record = ros_kernel_gui_shared_input_record_at_volatile(region, (uint32_t)(head_sequence % region->capacity));
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
		if (processed_count >= GWES_INPUT_BATCH_LIMIT) {
			break;
		}
	}

	consumer->head_sequence = head_sequence;
	consumer->last_seen_msec = (unsigned int)now;
	return processed_count;
}

/*
 * Dispatch one queued shared-input event on the window and render owner thread.
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
