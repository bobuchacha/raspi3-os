#include "user_runtime.h"
#include "app/kernel_gui.h"

DLL_EXPORT(Init);
DLL_EXPORT(Deinit);
DLL_EXPORT(gui_init);
DLL_EXPORT(gui_taskbar_show);
DLL_EXPORT(gui_start_menu_show);
DLL_EXPORT(gui_soft_keyboard_show);
DLL_EXPORT(gui_taskbar_set_clock_date);
DLL_EXPORT(gui_taskbar_tick);
DLL_EXPORT(gui_display_info_query);
DLL_EXPORT(gui_pixels_present);
DLL_EXPORT(gui_input_event_poll);
DLL_EXPORT(gui_pointer_query);
DLL_EXPORT(gui_widget_frame_present);
DLL_EXPORT(gui_widget_event_poll);
DLL_EXPORT(gui_widget_focus_set);
DLL_EXPORT(gui_widget_pointer_capture);
DLL_EXPORT(gui_widget_invalidate);

static int g_gui_input_acquired;

int Init(void* base) {
    (void)base;
    g_gui_input_acquired = 0;
    return 0;
}

int Deinit(void* base) {
    (void)base;
    g_gui_input_acquired = 0;
    return 0;
}

long gui_init(void) {
    g_gui_input_acquired = 0;
    return 0;
}

long gui_taskbar_show(unsigned long visible) {
    (void)visible;
    return 0;
}

long gui_start_menu_show(unsigned long visible) {
    (void)visible;
    return 0;
}

long gui_soft_keyboard_show(unsigned long visible) {
    (void)visible;
    return 0;
}

long gui_taskbar_set_clock_date(const char* clock_text, const char* date_text) {
    (void)clock_text;
    (void)date_text;
    return 0;
}

long gui_taskbar_tick(void) {
    return 0;
}

long gui_display_info_query(RosKernelGuiDisplayInfo* info) {
    if (!info) {
        return -1;
    }

    return controlGui(ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO, (unsigned long)info);
}

long gui_pixels_present(const RosKernelGuiPresentBuffer* buffer) {
    if (!buffer) {
        return -1;
    }

    return controlGui(ROS_KERNEL_GUI_CONTROL_DISPLAY_PRESENT, (unsigned long)buffer);
}

long gui_input_event_poll(RosKernelGuiInputEvent* event) {
    if (!event) {
        return -1;
    }

    // Claim the raw input stream only when this process actually starts polling it.
    if (!g_gui_input_acquired) {
        if (controlGui(ROS_KERNEL_GUI_CONTROL_INPUT_ACQUIRE, 0UL) != 0) {
            return -1;
        }
        g_gui_input_acquired = 1;
    }

    return controlGui(ROS_KERNEL_GUI_CONTROL_INPUT_EVENT_POLL, (unsigned long)event);
}

long gui_pointer_query(RosKernelGuiPointerState* state) {
    if (!state) {
        return -1;
    }

    return controlGui(ROS_KERNEL_GUI_CONTROL_RAW_POINTER_QUERY, (unsigned long)state);
}

long gui_widget_frame_present(const RosKernelGuiWidgetFrame* frame) {
    (void)frame;
    return -1;
}

long gui_widget_event_poll(RosKernelGuiWidgetEvent* event) {
    (void)event;
    return -1;
}

long gui_widget_focus_set(unsigned long control_id) {
    (void)control_id;
    return -1;
}

long gui_widget_pointer_capture(unsigned long control_id) {
    (void)control_id;
    return -1;
}

long gui_widget_invalidate(unsigned long reserved) {
    (void)reserved;
    return -1;
}