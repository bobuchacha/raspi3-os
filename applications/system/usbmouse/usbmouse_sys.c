#include "app/kernel_module.h"

extern int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);

static const RosKernelModuleApi* g_api;

static const RosKernelModuleEmbeddedMetadata g_usbmouse_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "usbmouse",
    "usbmouse_init",
    "usbmouse_shutdown",
    "",
    2,
    {
        ROS_KERNEL_MODULE_EXPORT("inject_pointer", usbmouse_inject_pointer),
        ROS_KERNEL_MODULE_EXPORT("present", usbmouse_present),
        { "", "" },
        { "", "" },
    },
};

int usbmouse_init(const RosKernelModuleApi* api) {
    if (!api || !api->logf) {
        return -1;
    }

    g_api = api;
    g_api->logf("INFO", "usbmouse", "pointer bridge ready");
    return 0;
}

void usbmouse_shutdown(void) {
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "usbmouse", "USB pointer bridge shutdown");
    }
}

long usbmouse_inject_pointer(unsigned long x, unsigned long y) {
    long result = 0;

    if (module_invoke("fbgui", "push_pointer", x, y, &result) != 0) {
        return -1;
    }
    return result;
}

long usbmouse_present(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return 1;
}
