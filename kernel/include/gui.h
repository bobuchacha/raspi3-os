#ifndef RASPI3_OS_GUI_H
#define RASPI3_OS_GUI_H

#include "ros.h"

#define GUI_POINTER_HIDDEN ((ULong)~0UL)

int gui_is_ready(void);
void gui_reset(void);
long gui_key(unsigned long key, unsigned long unused);
long gui_pointer(unsigned long x, unsigned long y);
long gui_usb_enabled(unsigned long unused0, unsigned long unused1);

#endif
