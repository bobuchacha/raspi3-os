#ifndef ROS_APP_KERNEL_GUI_H
#define ROS_APP_KERNEL_GUI_H

#include "stdint.h"

#define ROS_KERNEL_GUI_POINTER_HIDDEN ((uint64_t)~0ULL)

#define ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO 1UL
#define ROS_KERNEL_GUI_CONTROL_DISPLAY_PRESENT 2UL
#define ROS_KERNEL_GUI_CONTROL_INPUT_EVENT_POLL 3UL
#define ROS_KERNEL_GUI_CONTROL_RAW_POINTER_QUERY 4UL
#define ROS_KERNEL_GUI_CONTROL_INPUT_ACQUIRE 16UL
#define ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_ACQUIRE 17UL
#define ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_RELEASE 18UL
#define ROS_KERNEL_GUI_CONTROL_SHARED_INPUT_QUERY 19UL

#define ROS_KERNEL_GUI_DISPLAY_INFO_VERSION 1U
#define ROS_KERNEL_GUI_PRESENT_BUFFER_VERSION 1U
#define ROS_KERNEL_GUI_INPUT_EVENT_VERSION 1U

#define ROS_KERNEL_GUI_PIXEL_FORMAT_XRGB8888 1U
#define ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888 2U

#define ROS_KERNEL_GUI_INPUT_EVENT_NONE 0U
#define ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE 1U
#define ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN 2U
#define ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP 3U
#define ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE 4U
#define ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN 5U
#define ROS_KERNEL_GUI_INPUT_EVENT_KEY_UP 6U

#define ROS_KERNEL_GUI_SHARED_INPUT_MAGIC 0x53474955U
#define ROS_KERNEL_GUI_SHARED_INPUT_VERSION 1U
#define ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS 8U
#define ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY 64U

typedef struct RosKernelGuiPointerStateStruct {
    uint32_t x;
    uint32_t y;
    uint32_t pressed;
    uint32_t visible;
} RosKernelGuiPointerState;

typedef RosKernelGuiPointerState RosKernelGuiDesktopPointerState;

typedef struct RosKernelGuiDisplayInfoStruct {
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pixel_format;
} RosKernelGuiDisplayInfo;

typedef struct RosKernelGuiPresentBufferStruct {
    uint32_t version;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t pixels;
    uint32_t reserved;
} RosKernelGuiPresentBuffer;

typedef struct RosKernelGuiInputEventStruct {
    uint32_t version;
    uint32_t type;
    uint32_t x;
    uint32_t y;
    uint32_t key;
    uint32_t buttons;
    uint32_t reserved;
} RosKernelGuiInputEvent;

typedef struct RosKernelGuiSharedInputConsumerStruct {
    uint32_t pid;
    uint32_t flags;
    uint64_t head_sequence;
    uint64_t drop_count;
    uint32_t last_seen_msec;
    uint32_t reserved;
} RosKernelGuiSharedInputConsumer;

typedef struct RosKernelGuiSharedInputRecordStruct {
    uint64_t sequence;
    uint32_t uptime_msec;
    uint32_t reserved0;
    RosKernelGuiInputEvent event;
    uint32_t reserved1;
} RosKernelGuiSharedInputRecord;

typedef struct RosKernelGuiSharedInputRegionStruct {
    uint32_t magic;
    uint16_t version;
    uint16_t max_consumers;
    uint32_t capacity;
    uint32_t record_size;
    uint64_t tail_sequence;
    uint64_t produced_count;
    uint64_t overflow_count;
    uint64_t pointer_event_count;
    uint64_t key_event_count;
    RosKernelGuiPointerState last_pointer_state;
    uint32_t reserved0;
    uint32_t reserved1;
    RosKernelGuiSharedInputConsumer consumers[ROS_KERNEL_GUI_SHARED_INPUT_MAX_CONSUMERS];
    RosKernelGuiSharedInputRecord records[ROS_KERNEL_GUI_SHARED_INPUT_CAPACITY];
} RosKernelGuiSharedInputRegion;

typedef struct RosKernelGuiSharedInputViewStruct {
    uint32_t version;
    uint32_t flags;
    uint64_t view_address;
    uint32_t view_size;
    uint32_t consumer_index;
    uint64_t initial_head_sequence;
} RosKernelGuiSharedInputView;

#define ROS_KERNEL_GUI_TASKBAR_TEXT_MAX 32U

#define ROS_KERNEL_GUI_CONTROL_TASKBAR_VISIBLE 1UL
#define ROS_KERNEL_GUI_CONTROL_START_MENU_VISIBLE 2UL
#define ROS_KERNEL_GUI_CONTROL_SOFT_KEYBOARD_VISIBLE 3UL
#define ROS_KERNEL_GUI_CONTROL_DESKTOP_VISIBLE 4UL
#define ROS_KERNEL_GUI_CONTROL_DESKTOP_PRESENT 5UL
#define ROS_KERNEL_GUI_CONTROL_POINTER_QUERY 6UL
#define ROS_KERNEL_GUI_CONTROL_WIDGET_FRAME_PRESENT 7UL
#define ROS_KERNEL_GUI_CONTROL_WIDGET_EVENT_POLL 8UL
#define ROS_KERNEL_GUI_CONTROL_WIDGET_FOCUS_SET 9UL
#define ROS_KERNEL_GUI_CONTROL_WIDGET_POINTER_CAPTURE 10UL
#define ROS_KERNEL_GUI_CONTROL_WIDGET_INVALIDATE 11UL

#define ROS_KERNEL_GUI_DESKTOP_FRAME_VERSION 1U
#define ROS_KERNEL_GUI_DESKTOP_WINDOW_MAX 5U
#define ROS_KERNEL_GUI_DESKTOP_WINDOW_TITLE_MAX 32U
#define ROS_KERNEL_GUI_DESKTOP_WINDOW_LINE_MAX 12U
#define ROS_KERNEL_GUI_DESKTOP_TEXT_MAX 80U
#define ROS_KERNEL_GUI_DESKTOP_TITLEBAR_HEIGHT 24U
#define ROS_KERNEL_GUI_DESKTOP_LINE_HEIGHT 18U
#define ROS_KERNEL_GUI_DESKTOP_PADDING 8U

#define ROS_KERNEL_GUI_WIDGET_FRAME_VERSION 1U
#define ROS_KERNEL_GUI_WIDGET_WINDOW_MAX 6U
#define ROS_KERNEL_GUI_WIDGET_CONTROL_MAX 24U
#define ROS_KERNEL_GUI_WIDGET_TITLE_MAX 48U
#define ROS_KERNEL_GUI_WIDGET_TEXT_MAX 96U

#define ROS_KERNEL_GUI_WIDGET_WINDOW_FLAG_VISIBLE (1U << 0)
#define ROS_KERNEL_GUI_WIDGET_WINDOW_FLAG_ACTIVE (1U << 1)
#define ROS_KERNEL_GUI_WIDGET_WINDOW_FLAG_RESIZABLE (1U << 2)
#define ROS_KERNEL_GUI_WIDGET_WINDOW_FLAG_MOVABLE (1U << 3)
#define ROS_KERNEL_GUI_WIDGET_WINDOW_FLAG_CLOSE_BUTTON (1U << 4)

#define ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_VISIBLE (1U << 0)
#define ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_ENABLED (1U << 1)
#define ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_FOCUSED (1U << 2)
#define ROS_KERNEL_GUI_WIDGET_CONTROL_FLAG_BORDER (1U << 3)

#define ROS_KERNEL_GUI_WIDGET_KIND_NONE 0U
#define ROS_KERNEL_GUI_WIDGET_KIND_LABEL 1U
#define ROS_KERNEL_GUI_WIDGET_KIND_BUTTON 2U
#define ROS_KERNEL_GUI_WIDGET_KIND_TEXTBOX 3U
#define ROS_KERNEL_GUI_WIDGET_KIND_LIST 4U
#define ROS_KERNEL_GUI_WIDGET_KIND_PANEL 5U

#define ROS_KERNEL_GUI_WIDGET_EVENT_NONE 0U
#define ROS_KERNEL_GUI_WIDGET_EVENT_POINTER_MOVE 1U
#define ROS_KERNEL_GUI_WIDGET_EVENT_POINTER_DOWN 2U
#define ROS_KERNEL_GUI_WIDGET_EVENT_POINTER_UP 3U
#define ROS_KERNEL_GUI_WIDGET_EVENT_KEY_DOWN 4U
#define ROS_KERNEL_GUI_WIDGET_EVENT_KEY_UP 5U
#define ROS_KERNEL_GUI_WIDGET_EVENT_WINDOW_CLOSE 6U
#define ROS_KERNEL_GUI_WIDGET_EVENT_WINDOW_MOVED 7U
#define ROS_KERNEL_GUI_WIDGET_EVENT_WINDOW_RESIZED 8U
#define ROS_KERNEL_GUI_WIDGET_EVENT_CONTROL_CLICKED 9U
#define ROS_KERNEL_GUI_WIDGET_EVENT_CONTROL_CHANGED 10U

#define ROS_KERNEL_GUI_WIDGET_RESIZE_NONE 0U
#define ROS_KERNEL_GUI_WIDGET_RESIZE_LEFT (1U << 0)
#define ROS_KERNEL_GUI_WIDGET_RESIZE_TOP (1U << 1)
#define ROS_KERNEL_GUI_WIDGET_RESIZE_RIGHT (1U << 2)
#define ROS_KERNEL_GUI_WIDGET_RESIZE_BOTTOM (1U << 3)

typedef struct RosKernelGuiWidgetRectStruct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} RosKernelGuiWidgetRect;

typedef struct RosKernelGuiWidgetControlStruct {
    uint32_t id;
    uint32_t parentId;
    uint32_t kind;
    uint32_t flags;
    RosKernelGuiWidgetRect bounds;
    uint32_t backgroundColor;
    uint32_t foregroundColor;
    uint32_t accentColor;
    uint32_t textLength;
    uint32_t value;
    uint32_t reserved0;
    uint32_t reserved1;
    char text[ROS_KERNEL_GUI_WIDGET_TEXT_MAX];
} RosKernelGuiWidgetControl;

typedef struct RosKernelGuiWidgetWindowStruct {
    uint32_t id;
    uint32_t flags;
    uint32_t zOrder;
    uint32_t controlCount;
    RosKernelGuiWidgetRect bounds;
    uint32_t minWidth;
    uint32_t minHeight;
    uint32_t titleColor;
    uint32_t frameColor;
    uint32_t backgroundColor;
    char title[ROS_KERNEL_GUI_WIDGET_TITLE_MAX];
    RosKernelGuiWidgetControl controls[ROS_KERNEL_GUI_WIDGET_CONTROL_MAX];
} RosKernelGuiWidgetWindow;

typedef struct RosKernelGuiWidgetFrameStruct {
    uint32_t version;
    uint32_t backgroundColor;
    uint32_t windowCount;
    uint32_t focusedWindowId;
    uint32_t focusedControlId;
    uint32_t pointerCaptureWindowId;
    uint32_t pointerCaptureControlId;
    uint32_t reserved;
    RosKernelGuiWidgetWindow windows[ROS_KERNEL_GUI_WIDGET_WINDOW_MAX];
} RosKernelGuiWidgetFrame;

typedef struct RosKernelGuiWidgetEventStruct {
    uint32_t version;
    uint32_t type;
    uint32_t windowId;
    uint32_t controlId;
    uint32_t pointerX;
    uint32_t pointerY;
    uint32_t key;
    uint32_t buttons;
    uint32_t resizeEdges;
    uint32_t value0;
    uint32_t value1;
    uint32_t reserved;
} RosKernelGuiWidgetEvent;

typedef struct RosKernelGuiDesktopWindowStruct {
    uint32_t visible;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t accentColor;
    uint32_t lineCount;
    char title[ROS_KERNEL_GUI_DESKTOP_WINDOW_TITLE_MAX];
    char lines[ROS_KERNEL_GUI_DESKTOP_WINDOW_LINE_MAX][ROS_KERNEL_GUI_DESKTOP_TEXT_MAX];
} RosKernelGuiDesktopWindow;

typedef struct RosKernelGuiDesktopFrameStruct {
    uint32_t version;
    uint32_t backgroundColor;
    uint32_t windowCount;
    uint32_t reserved;
    RosKernelGuiDesktopWindow windows[ROS_KERNEL_GUI_DESKTOP_WINDOW_MAX];
} RosKernelGuiDesktopFrame;

#endif