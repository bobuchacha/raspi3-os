#include "app/kernel_module.h"

static const RosKernelModuleApi* g_api;
static uint64_t g_next_heartbeat_ms;

static const RosKernelModuleEmbeddedMetadata g___SYS_NAME___metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "__SYS_NAME__",
    "__SYS_NAME___init",
    "__SYS_NAME___shutdown",
    "__SYS_NAME___idle",
    0,
    {
        { "", "" },
        { "", "" },
        { "", "" },
        { "", "" },
    },
};

int __SYS_NAME___init(const RosKernelModuleApi* api) {
    if (!api || !api->logf) {
        return -1;
    }

    g_api = api;
    g_next_heartbeat_ms = 0;
    g_api->logf("INFO", "__SYS_NAME__", "module initialized");
    return 0;
}

void __SYS_NAME___shutdown(void) {
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "__SYS_NAME__", "module shutdown");
    }
}

void __SYS_NAME___idle(uint64_t now_ms) {
    if (!g_api || !g_api->logf) {
        return;
    }
    if (g_next_heartbeat_ms != 0 && now_ms < g_next_heartbeat_ms) {
        return;
    }

    g_api->logf("DEBUG", "__SYS_NAME__", "idle heartbeat at %llu ms", (unsigned long long)now_ms);
    g_next_heartbeat_ms = now_ms + 5000ULL;
}
