#include "app/kernel_module.h"

extern int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);

static const RosKernelModuleApi* g_api;

static const RosKernelModuleEmbeddedMetadata g_fbgui_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "fbgui",
    "fbgui_init",
    "fbgui_shutdown",
    "",
    2,
    {
        ROS_KERNEL_MODULE_EXPORT("push_key", fbgui_push_key),
        ROS_KERNEL_MODULE_EXPORT("push_pointer", fbgui_push_pointer),
        { "", "" },
        { "", "" },
    },
};

static int fbgui_kernel_call(const char* export_name, unsigned long a, unsigned long b, long* result) {
    if (module_invoke("kernel", export_name, a, b, result) != 0) {
        return -1;
    }
    return 0;
}

int fbgui_init(const RosKernelModuleApi* api) {
    long ready = 0;

    if (!api || !api->logf) {
        return -1;
    }

    g_api = api;
    if (fbgui_kernel_call("gui_ready", 0, 0, &ready) != 0 || ready == 0) {
        g_api->logf("WARN", "fbgui", "framebuffer not ready");
        return -1;
    }

    if (fbgui_kernel_call("gui_reset", 0, 0, &ready) != 0) {
        g_api->logf("WARN", "fbgui", "unable to initialize framebuffer console");
        return -1;
    }

    g_api->logf("INFO", "fbgui", "framebuffer console ready");
    return 0;
}

void fbgui_shutdown(void) {
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "fbgui", "framebuffer console shutdown");
    }
}

long fbgui_push_key(unsigned long key, unsigned long unused) {
    long result = 0;

    (void)unused;
    if (fbgui_kernel_call("gui_key", key, 0, &result) != 0) {
        return -1;
    }
    return result;
}

long fbgui_push_pointer(unsigned long x, unsigned long y) {
    long result = 0;

    if (fbgui_kernel_call("gui_pointer", x, y, &result) != 0) {
        return -1;
    }
    return result;
}
