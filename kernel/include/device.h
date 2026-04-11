/**
 * device.h
 *
 * This is the header file for device management in the kernel.
 * It includes kernel interface and device management procedures
 */

#ifndef KERNEL_INCLUDE_DEVICE_H
#define KERNEL_INCLUDE_DEVICE_H

#include "types.h"

typedef struct Device Device;
typedef struct DeviceDriver DeviceDriver;
typedef struct DeviceInterface DeviceInterface;

/**
 * Device Descriptor
 */
struct DeviceInterface {
    Status(*init)(Device* device);
    Status(*deinit)(Device* device);
    SSize(*read)(Device* device, U64 offset, void* buffer, Size length);
    SSize(*write)(Device* device, U64 offset, const void* buffer, Size length);
    Status(*ioctl)(Device* device, U32 request, void* argument);
};

struct DeviceDriver {
    const char* name;
    const DeviceInterface* interface;
    void* state;

    Status init(Device* device) const {
        if ((interface == NULL) || (interface->init == NULL)) {
            return StatusNotSupported;
        }

        return interface->init(device);
    }

    Status deinit(Device* device) const {
        if ((interface == NULL) || (interface->deinit == NULL)) {
            return StatusNotSupported;
        }

        return interface->deinit(device);
    }

    SSize read(Device* device, U64 offset, void* buffer, Size length) const {
        if ((interface == NULL) || (interface->read == NULL)) {
            return StatusNotSupported;
        }

        return interface->read(device, offset, buffer, length);
    }

    SSize write(Device* device, U64 offset, const void* buffer, Size length) const {
        if ((interface == NULL) || (interface->write == NULL)) {
            return StatusNotSupported;
        }

        return interface->write(device, offset, buffer, length);
    }

    Status ioctl(Device* device, U32 request, void* argument) const {
        if ((interface == NULL) || (interface->ioctl == NULL)) {
            return StatusNotSupported;
        }

        return interface->ioctl(device, request, argument);
    }
    };

struct Device {
    const char* name;
    const DeviceInterface* interface;
    void* state;
    U32 irq;
    DeviceDriver* driver;

    const DeviceInterface* active_interface(void) const {
        if (interface != NULL) {
            return interface;
        }
        if (driver != NULL) {
            return driver->interface;
        }
        return NULL;
    }

    Status init(void) {
        const DeviceInterface* active = active_interface();

        if ((active == NULL) || (active->init == NULL)) {
            return StatusNotSupported;
        }

        return active->init(this);
    }

    Status deinit(void) {
        const DeviceInterface* active = active_interface();

        if ((active == NULL) || (active->deinit == NULL)) {
            return StatusNotSupported;
        }

        return active->deinit(this);
    }

    SSize read(U64 offset, void* buffer, Size length) {
        const DeviceInterface* active = active_interface();

        if ((active == NULL) || (active->read == NULL)) {
            return StatusNotSupported;
        }

        return active->read(this, offset, buffer, length);
    }

    SSize write(U64 offset, const void* buffer, Size length) {
        const DeviceInterface* active = active_interface();

        if ((active == NULL) || (active->write == NULL)) {
            return StatusNotSupported;
        }

        return active->write(this, offset, buffer, length);
    }

    Status ioctl(U32 request, void* argument) {
        const DeviceInterface* active = active_interface();

        if ((active == NULL) || (active->ioctl == NULL)) {
            return StatusNotSupported;
        }

        return active->ioctl(this, request, argument);
    }
};

class DeviceManager final {
public:
    static Status init(void);
    static Status register_device(Device* device);
    static Status register_driver(DeviceDriver* driver);
    static Status bind_driver(Device* device, DeviceDriver* driver);
    static Device* find_device(const char* name);
    static DeviceDriver* find_driver(const char* name);
};

#endif // KERNEL_INCLUDE_DEVICE_H
