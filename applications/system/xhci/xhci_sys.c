#include "app/kernel_module.h"

extern int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);

static const RosKernelModuleApi* g_api;
static int g_present;

static const RosKernelModuleEmbeddedMetadata g_xhci_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "xhci",
    "xhci_init",
    "xhci_shutdown",
    "",
    1,
    {
        ROS_KERNEL_MODULE_EXPORT("present", xhci_present),
        { "", "" },
        { "", "" },
        { "", "" },
    },
};

int xhci_init(const RosKernelModuleApi* api) {
    long enabled = 0;

    if (!api || !api->logf) {
        return -1;
    }

    g_api = api;
    if (module_invoke("kernel", "usb_enabled", 0, 0, &enabled) != 0 || enabled == 0) {
        g_present = 0;
        g_api->logf("INFO", "xhci", "xHCI unavailable on current run target; bridge stays inactive");
        return 0;
    }

    g_present = 1;
    g_api->logf("INFO", "xhci", "xHCI controller target enabled");
    return 0;
}

void xhci_shutdown(void) {
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "xhci", "xHCI controller shutdown");
    }
}

long xhci_present(unsigned long unused0, unsigned long unused1) {
    (void)unused0;
    (void)unused1;
    return g_present ? 1 : 0;
}
