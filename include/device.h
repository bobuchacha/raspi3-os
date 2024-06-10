#ifndef _DEVICE_H
#define _DEVICE_H

#include "device/raspi3b.h"             // this is our device
#include "arch/cortex-a53/mmu.h"                // we use this arch


void device_init();                     // initialize device

#endif