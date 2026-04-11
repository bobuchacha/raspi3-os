#ifndef KERNEL_INTERNAL_DEVICE_REGISTRY_H
#define KERNEL_INTERNAL_DEVICE_REGISTRY_H

#include "device.h"

typedef struct DeviceNode {
    Device* device;
    struct DeviceNode* next;
} DeviceNode;

#endif // KERNEL_INTERNAL_DEVICE_REGISTRY_H
