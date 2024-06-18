#ifndef _DEVICE_H
#define _DEVICE_H

#include "ros.h"
#include "device/raspi3b.h"             // this is our device
#include "arch/cortex-a53/mmu.h"                // we use this arch

void device_init();                     // initialize device
void device_init_fs();

#endif