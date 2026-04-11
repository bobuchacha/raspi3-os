#ifndef ROS_LOADER_VIRT_H
#define ROS_LOADER_VIRT_H

#include "device/virt/peripherals/base.h"
#include "device/virt/peripherals/uart0.h"

#define DEVICE_MEMORY_SIZE 0x10000000UL

#define VIRT_MMIO_BASE 0x08000000UL
#define VIRT_MMIO_LIMIT 0x10000000UL
#define VIRT_RAM_BASE 0x40000000UL
#define VIRT_RAM_SIZE 0x10000000UL

#endif