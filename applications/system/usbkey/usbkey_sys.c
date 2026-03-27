#include "app/kernel_module.h"

extern int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);

static const RosKernelModuleApi* g_api;

static const RosKernelModuleEmbeddedMetadata g_usbkey_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "usbkey",
    "usbkey_init",
    "usbkey_shutdown",
    "",
    2,
    {
        ROS_KERNEL_MODULE_EXPORT("inject_key", usbkey_inject_key),
        ROS_KERNEL_MODULE_EXPORT("present", usbkey_present),
        { "", "" },
        { "", "" },
    },
};

int usbkey_init(const RosKernelModuleApi* api) {
    if (!api || !api->logf) {
        return -1;
    }

    g_api = api;
    g_api->logf("INFO", "usbkey", "keyboard bridge ready");
    return 0;
}

void usbkey_shutdown(void) {
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "usbkey", "USB keyboard bridge shutdown");
    }
}

long usbkey_inject_key(unsigned long key, unsigned long unused) {
    long result = 0;

    (void)unused;
    if (module_invoke("fbgui", "push_key", key, 0, &result) != 0) {
        return -1;
    }
    return result;
}

long usbkey_present(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return 1;
}
