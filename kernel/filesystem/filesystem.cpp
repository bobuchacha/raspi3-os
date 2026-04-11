#include "filesystem.h"

#include "kernel_event_broker.h"

namespace {

    FilesystemDriver* g_filesystems[16];
    Size g_filesystem_count;

} // namespace

Status FilesystemRegistry::init(void) {
    g_filesystem_count = 0;
    memzero(g_filesystems, sizeof(g_filesystems));
    return StatusOK;
}

Status FilesystemRegistry::register_driver(FilesystemDriver* driver) {
    if (driver == NULL) {
        return StatusInvalidArgument;
    }
    if (g_filesystem_count == COUNT_OF(g_filesystems)) {
        return StatusNoSpace;
    }

    g_filesystems[g_filesystem_count++] = driver;
    KernelEventBroker::publish_registration_event(KernelEventTypeFilesystemDriverRegistered, driver->name, StatusOK);
    return StatusOK;
}