#include "device.h"

#include "heap.h"
#include "kernel_event_broker.h"

namespace {

    typedef struct DeviceRegistryEntry {
        Device* device;
        struct DeviceRegistryEntry* next;
    } DeviceRegistryEntry;

    typedef struct DriverRegistryEntry {
        DeviceDriver* driver;
        struct DriverRegistryEntry* next;
    } DriverRegistryEntry;

    DeviceRegistryEntry* g_device_head;
    Size g_device_count;
    DriverRegistryEntry* g_driver_head;
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

    /**
     * Release every heap-backed device registry entry.
     *
     * `DeviceManager::init()` is still written as a full reset hook, so the
     * dynamic registry must drop any previously allocated nodes before the
     * kernel starts re-registering devices during a reinitialization pass.
     *
     * @return Nothing.
     */
    void release_device_registry(void) {
        while (g_device_head != NULL) {
            DeviceRegistryEntry* next = g_device_head->next;

            Heap::free(g_device_head);
            g_device_head = next;
        }
    }

    /**
     * Release every heap-backed driver registry entry.
     *
     * @return Nothing.
     */
    void release_driver_registry(void) {
        while (g_driver_head != NULL) {
            DriverRegistryEntry* next = g_driver_head->next;

            Heap::free(g_driver_head);
            g_driver_head = next;
        }
    }

    /**
     * Allocate one registry node for a device pointer.
     *
     * @param device Device to publish through the registry.
     * @return Heap-backed registry node, or NULL on allocation failure.
     */
    DeviceRegistryEntry* allocate_device_entry(Device* device) {
        DeviceRegistryEntry* entry = static_cast<DeviceRegistryEntry*>(Heap::alloc(sizeof(DeviceRegistryEntry), alignof(DeviceRegistryEntry)));

        if (entry == NULL) {
            return NULL;
        }

        entry->device = device;
        entry->next = NULL;
        return entry;
    }

    /**
     * Allocate one registry node for a driver pointer.
     *
     * @param driver Driver to publish through the registry.
     * @return Heap-backed registry node, or NULL on allocation failure.
     */
    DriverRegistryEntry* allocate_driver_entry(DeviceDriver* driver) {
        DriverRegistryEntry* entry = static_cast<DriverRegistryEntry*>(Heap::alloc(sizeof(DriverRegistryEntry), alignof(DriverRegistryEntry)));

        if (entry == NULL) {
            return NULL;
        }

        entry->driver = driver;
        entry->next = NULL;
        return entry;
    }

} // namespace

Status DeviceManager::init(void) {
    release_device_registry();
    release_driver_registry();
    g_device_head = NULL;
    g_driver_head = NULL;
    g_device_count = 0;
    g_driver_count = 0;
    return StatusOK;
}

Status DeviceManager::register_device(Device* device) {
    Status status;

    if (device == NULL) {
        return StatusInvalidArgument;
    }

    for (DeviceRegistryEntry* entry = g_device_head; entry != NULL; entry = entry->next) {
        if (entry->device == device) {
            return StatusAlreadyExists;
        }
    }

    if (device->driver != NULL) {
        status = register_driver(device->driver);
        if ((status != StatusOK) && (status != StatusAlreadyExists)) {
            return status;
        }
    }

    {
        DeviceRegistryEntry* entry = allocate_device_entry(device);

        if (entry == NULL) {
            return StatusNoMemory;
        }

        entry->next = g_device_head;
        g_device_head = entry;
        ++g_device_count;
    }

    // Emit registration once the device table owns the slot so later lookups match what subscribers observe.
    KernelEventBroker::publish_registration_event(KernelEventTypeDeviceRegistered, device->name, StatusOK);
    return StatusOK;
}

Status DeviceManager::register_driver(DeviceDriver* driver) {
    if (driver == NULL) {
        return StatusInvalidArgument;
    }

    for (DriverRegistryEntry* entry = g_driver_head; entry != NULL; entry = entry->next) {
        if (entry->driver == driver) {
            return StatusAlreadyExists;
        }
    }

    {
        DriverRegistryEntry* entry = allocate_driver_entry(driver);

        if (entry == NULL) {
            return StatusNoMemory;
        }

        entry->next = g_driver_head;
        g_driver_head = entry;
        ++g_driver_count;
    }

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

    for (DeviceRegistryEntry* entry = g_device_head; entry != NULL; entry = entry->next) {
        if ((entry->device != NULL) && same_text(entry->device->name, name)) {
            return entry->device;
        }
    }

    return NULL;
}

DeviceDriver* DeviceManager::find_driver(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (DriverRegistryEntry* entry = g_driver_head; entry != NULL; entry = entry->next) {
        if ((entry->driver != NULL) && same_text(entry->driver->name, name)) {
            return entry->driver;
        }
    }

    return NULL;
}