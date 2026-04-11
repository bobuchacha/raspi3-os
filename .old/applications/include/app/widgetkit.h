#ifndef ROS_APP_WIDGETKIT_H
#define ROS_APP_WIDGETKIT_H

#include "app/gui.h"

typedef RosKernelGuiWidgetRect WidgetKitRect;
typedef RosKernelGuiWidgetControl WidgetKitControl;
typedef RosKernelGuiWidgetWindow WidgetKitWindow;
typedef RosKernelGuiWidgetFrame WidgetKitFrame;
typedef RosKernelGuiWidgetEvent WidgetKitEvent;

static inline void widgetKitClearBytes(void* buffer, unsigned long size) {
    unsigned char* bytes = (unsigned char*)buffer;

    if (!bytes) {
        return;
    }

    for (unsigned long index = 0UL; index < size; index++) {
        bytes[index] = 0U;
    }
}

static inline void widgetKitCopyText(char* destination, unsigned long size, const char* text, unsigned int* out_length) {
    unsigned long index = 0UL;

    if (out_length) {
        *out_length = 0U;
    }
    if (!destination || size == 0UL) {
        return;
    }
    if (!text) {
        destination[0] = '\0';
        return;
    }

    while (index + 1UL < size && text[index] != '\0') {
        destination[index] = text[index];
        index++;
    }
    destination[index] = '\0';
    if (out_length) {
        *out_length = (unsigned int)index;
    }
}

static inline WidgetKitRect widgetKitMakeRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    WidgetKitRect rect;

    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

static inline void widgetKitResetFrame(WidgetKitFrame* frame, uint32_t background_color) {
    if (!frame) {
        return;
    }

    widgetKitClearBytes(frame, sizeof(*frame));
    frame->version = ROS_KERNEL_GUI_WIDGET_FRAME_VERSION;
    frame->backgroundColor = background_color;
}

static inline WidgetKitWindow* widgetKitAddWindow(
    WidgetKitFrame* frame,
    uint32_t id,
    const char* title,
    WidgetKitRect bounds,
    uint32_t min_width,
    uint32_t min_height,
    uint32_t flags,
    uint32_t title_color,
    uint32_t frame_color,
    uint32_t background_color
) {
    WidgetKitWindow* window;

    if (!frame || frame->windowCount >= ROS_KERNEL_GUI_WIDGET_WINDOW_MAX) {
        return 0;
    }

    window = &frame->windows[frame->windowCount++];
    widgetKitClearBytes(window, sizeof(*window));
    window->id = id;
    window->flags = flags;
    window->bounds = bounds;
    window->minWidth = min_width;
    window->minHeight = min_height;
    window->titleColor = title_color;
    window->frameColor = frame_color;
    window->backgroundColor = background_color;
    widgetKitCopyText(window->title, sizeof(window->title), title, 0);
    return window;
}

static inline WidgetKitControl* widgetKitAddControl(
    WidgetKitWindow* window,
    uint32_t id,
    uint32_t parent_id,
    uint32_t kind,
    WidgetKitRect bounds,
    uint32_t flags,
    const char* text,
    uint32_t background_color,
    uint32_t foreground_color,
    uint32_t accent_color,
    uint32_t value
) {
    WidgetKitControl* control;

    if (!window || window->controlCount >= ROS_KERNEL_GUI_WIDGET_CONTROL_MAX) {
        return 0;
    }

    control = &window->controls[window->controlCount++];
    widgetKitClearBytes(control, sizeof(*control));
    control->id = id;
    control->parentId = parent_id;
    control->kind = kind;
    control->flags = flags;
    control->bounds = bounds;
    control->backgroundColor = background_color;
    control->foregroundColor = foreground_color;
    control->accentColor = accent_color;
    control->value = value;
    widgetKitCopyText(control->text, sizeof(control->text), text, &control->textLength);
    return control;
}

static inline WidgetKitControl* widgetKitAddLabel(
    WidgetKitWindow* window,
    uint32_t id,
    WidgetKitRect bounds,
    const char* text,
    uint32_t foreground_color
) {
    return widgetKitAddControl(
        window,
        id,
        0U,
        ROS_KERNEL_GUI_WIDGET_KIND_LABEL,
        bounds,
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_VISIBLE,
        text,
        0U,
        foreground_color,
        0U,
        0U);
}

static inline WidgetKitControl* widgetKitAddButton(
    WidgetKitWindow* window,
    uint32_t id,
    WidgetKitRect bounds,
    const char* text,
    uint32_t background_color,
    uint32_t foreground_color,
    uint32_t accent_color
) {
    return widgetKitAddControl(
        window,
        id,
        0U,
        ROS_KERNEL_GUI_WIDGET_KIND_BUTTON,
        bounds,
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_VISIBLE |
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_ENABLED |
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_BORDER,
        text,
        background_color,
        foreground_color,
        accent_color,
        0U);
}

static inline WidgetKitControl* widgetKitAddTextBox(
    WidgetKitWindow* window,
    uint32_t id,
    WidgetKitRect bounds,
    const char* text,
    uint32_t background_color,
    uint32_t foreground_color,
    uint32_t accent_color
) {
    return widgetKitAddControl(
        window,
        id,
        0U,
        ROS_KERNEL_GUI_WIDGET_KIND_TEXTBOX,
        bounds,
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_VISIBLE |
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_ENABLED |
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_BORDER,
        text,
        background_color,
        foreground_color,
        accent_color,
        0U);
}

static inline WidgetKitControl* widgetKitAddList(
    WidgetKitWindow* window,
    uint32_t id,
    WidgetKitRect bounds,
    const char* text,
    uint32_t background_color,
    uint32_t foreground_color,
    uint32_t accent_color,
    uint32_t selected_index
) {
    return widgetKitAddControl(
        window,
        id,
        0U,
        ROS_KERNEL_GUI_WIDGET_KIND_LIST,
        bounds,
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_VISIBLE |
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_ENABLED |
        ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_BORDER,
        text,
        background_color,
        foreground_color,
        accent_color,
        selected_index);
}

static inline long widgetKitPresent(const WidgetKitFrame* frame) {
    return guiPresentWidgetFrame(frame);
}

static inline long widgetKitShowDesktop(unsigned long visible) {
    return controlGui(ROS_KERNEL_GUI_CONTROL_DESKTOP_VISIBLE, visible ? 1UL : 0UL);
}

static inline long widgetKitPollEvent(WidgetKitEvent* event) {
    return guiPollWidgetEvent(event);
}

static inline long widgetKitSetFocus(uint32_t control_id) {
    return guiSetWidgetFocus(control_id);
}

static inline long widgetKitSetPointerCapture(uint32_t control_id) {
    return guiSetWidgetPointerCapture(control_id);
}

static inline long widgetKitInvalidate(void) {
    return guiInvalidateWidgets(0UL);
}

#endif