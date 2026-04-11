#include "device.h"

#include "kernel_event_broker.h"

namespace {

    Device* g_devices[32];
    Size g_device_count;
    DeviceDriver* g_drivers[32];
    Size g_driver_count;

    bool same_text(const char* lhs, const char* rhs) {
        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((*lhs != '\0') && (*rhs != '\0')) {
            if (*lhs != *rhs) {
                return false;
            }
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }

} // namespace

Status DeviceManager::init(void) {
    g_device_count = 0;
    g_driver_count = 0;
    memzero(g_devices, sizeof(g_devices));
    memzero(g_drivers, sizeof(g_drivers));
    return StatusOK;
}

Status DeviceManager::register_device(Device* device) {
    Status status;

    if (device == NULL) {
        return StatusInvalidArgument;
    }

    for (Size index = 0; index < g_device_count; ++index) {
        if (g_devices[index] == device) {
            return StatusAlreadyExists;
        }
    }

    if (g_device_count == COUNT_OF(g_devices)) {
        return StatusNoSpace;
    }

    if (device->driver != NULL) {
        status = register_driver(device->driver);
        if ((status != StatusOK) && (status != StatusAlreadyExists)) {
            return status;
        }
    }

    g_devices[g_device_count++] = device;
    // Emit registration once the device table owns the slot so later lookups match what subscribers observe.
    KernelEventBroker::publish_registration_event(KernelEventTypeDeviceRegistered, device->name, StatusOK);
    return StatusOK;
}

Status DeviceManager::register_driver(DeviceDriver* driver) {
    if (driver == NULL) {
        return StatusInvalidArgument;
    }

    for (Size index = 0; index < g_driver_count; ++index) {
        if (g_drivers[index] == driver) {
            return StatusAlreadyExists;
        }
    }

    if (g_driver_count == COUNT_OF(g_drivers)) {
        return StatusNoSpace;
    }

    g_drivers[g_driver_count++] = driver;
    KernelEventBroker::publish_registration_event(KernelEventTypeDriverRegistered, driver->name, StatusOK);
    return StatusOK;
}

Status DeviceManager::bind_driver(Device* device, DeviceDriver* driver) {
    Status status;

    if ((device == NULL) || (driver == NULL)) {
        return StatusInvalidArgument;
    }

    status = register_driver(driver);
    if ((status != StatusOK) && (status != StatusAlreadyExists)) {
        return status;
    }

    device->driver = driver;
    return StatusOK;
}

Device* DeviceManager::find_device(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (Size index = 0; index < g_device_count; ++index) {
        if (same_text(g_devices[index]->name, name)) {
            return g_devices[index];
        }
    }

    return NULL;
}

DeviceDriver* DeviceManager::find_driver(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (Size index = 0; index < g_driver_count; ++index) {
        if (same_text(g_drivers[index]->name, name)) {
            return g_drivers[index];
        }
    }

    return NULL;
}