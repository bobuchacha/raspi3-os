#ifndef RASPI3_OS_GUI_H
#define RASPI3_OS_GUI_H

#include "ros.h"

#define GUI_POINTER_HIDDEN ((ULong)~0UL)

#define GUI_CONTROL_DISPLAY_INFO 1UL
#define GUI_CONTROL_DISPLAY_PRESENT 2UL
#define GUI_CONTROL_INPUT_EVENT_POLL 3UL
#define GUI_CONTROL_POINTER_QUERY 4UL
#define GUI_CONTROL_INPUT_ACQUIRE 16UL
#define GUI_CONTROL_SHARED_INPUT_ACQUIRE 17UL
#define GUI_CONTROL_SHARED_INPUT_RELEASE 18UL
#define GUI_CONTROL_SHARED_INPUT_QUERY 19UL

#define GUI_DISPLAY_INFO_VERSION 1U
#define GUI_PRESENT_BUFFER_VERSION 1U
#define GUI_INPUT_EVENT_VERSION 1U

#define GUI_PIXEL_FORMAT_XRGB8888 1U
#define GUI_PIXEL_FORMAT_XBGR8888 2U

#define GUI_INPUT_EVENT_NONE 0U
#define GUI_INPUT_EVENT_POINTER_MOVE 1U
#define GUI_INPUT_EVENT_POINTER_DOWN 2U
#define GUI_INPUT_EVENT_POINTER_UP 3U
#define GUI_INPUT_EVENT_POINTER_LEAVE 4U
#define GUI_INPUT_EVENT_KEY_DOWN 5U
#define GUI_INPUT_EVENT_KEY_UP 6U

#define GUI_SHARED_INPUT_MAGIC 0x53474955U
#define GUI_SHARED_INPUT_VERSION 1U
#define GUI_SHARED_INPUT_MAX_CONSUMERS 8U
#define GUI_SHARED_INPUT_CAPACITY 64U

typedef struct GuiPointerStateStruct {
    unsigned int x;
    unsigned int y;
    unsigned int pressed;
    unsigned int visible;
} GuiPointerState;

typedef GuiPointerState GuiDesktopPointerState;

typedef struct GuiDisplayInfoStruct {
    unsigned int version;
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int pixel_format;
} GuiDisplayInfo;

typedef struct GuiPresentBufferStruct {
    unsigned int version;
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    ULong pixels;
    unsigned int reserved;
} GuiPresentBuffer;

typedef struct GuiInputEventStruct {
    unsigned int version;
    unsigned int type;
    unsigned int x;
    unsigned int y;
    unsigned int key;
    unsigned int buttons;
    unsigned int reserved;
} GuiInputEvent;

typedef struct GuiSharedInputConsumerStruct {
    unsigned int pid;
    unsigned int flags;
    ULong head_sequence;
    ULong drop_count;
    unsigned int last_seen_msec;
    unsigned int reserved;
} GuiSharedInputConsumer;

typedef struct GuiSharedInputRecordStruct {
    ULong sequence;
    unsigned int uptime_msec;
    unsigned int reserved0;
    GuiInputEvent event;
    unsigned int reserved1;
} GuiSharedInputRecord;

typedef struct GuiSharedInputRegionStruct {
    unsigned int magic;
    unsigned short version;
    unsigned short max_consumers;
    unsigned int capacity;
    unsigned int record_size;
    ULong tail_sequence;
    ULong produced_count;
    ULong overflow_count;
    ULong pointer_event_count;
    ULong key_event_count;
    GuiPointerState last_pointer_state;
    unsigned int reserved0;
    unsigned int reserved1;
    GuiSharedInputConsumer consumers[GUI_SHARED_INPUT_MAX_CONSUMERS];
    GuiSharedInputRecord records[GUI_SHARED_INPUT_CAPACITY];
} GuiSharedInputRegion;

typedef struct GuiSharedInputViewStruct {
    unsigned int version;
    unsigned int flags;
    ULong view_address;
    unsigned int view_size;
    unsigned int consumer_index;
    ULong initial_head_sequence;
} GuiSharedInputView;

int gui_is_ready(void);
void gui_reset(void);
long gui_key(unsigned long key, unsigned long unused);
long gui_pointer_state(unsigned long x, unsigned long y, unsigned long pressed);
long gui_pointer(unsigned long x, unsigned long y);
long gui_usb_enabled(unsigned long unused0, unsigned long unused1);
long gui_control(unsigned long command, unsigned long value);
long gui_get_display_info(GuiDisplayInfo* info);
long gui_present_scanline(unsigned int x, unsigned int y, unsigned int pixel_count, const unsigned int* pixels);
long gui_get_input_event(GuiInputEvent* event);
long gui_get_pointer_state(GuiPointerState* state);
GuiSharedInputRegion* gui_shared_input_region(void);
long gui_shared_input_attach_consumer(unsigned long pid, GuiSharedInputView* view);
long gui_shared_input_release_consumer(unsigned long pid);
Address gui_shared_input_page_phys(unsigned int index);
unsigned int gui_shared_input_page_count(void);

#endif
