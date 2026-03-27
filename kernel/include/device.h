#ifndef _DEVICE_H
#define _DEVICE_H

#include "platform/board.h"
#include "platform/cpu/mmu.h"
#include "ros.h"

void device_init();
void device_init_fs();
void device_reboot();
int device_init_graphics(void);
int device_init_touch(void);
void device_poll_touch(void);

#endif