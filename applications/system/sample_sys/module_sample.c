#include "app/kernel_module.h"

static const RosKernelModuleApi* g_api;
static uint64_t g_next_heartbeat_ms;

static const RosKernelModuleEmbeddedMetadata g_sample_sys_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "sample_sys",
    "sample_sys_init",
    "sample_sys_shutdown",
    "sample_sys_idle",
    3,
    {
        ROS_KERNEL_MODULE_EXPORT("query_status", sample_sys_query_status),
        ROS_KERNEL_MODULE_EXPORT("do_add", do_add),
        ROS_KERNEL_MODULE_EXPORT("get_loadaddr", sample_sys_get_loaded_address),
    },
};

enum {
    SAMPLE_SYS_QUERY_ABI_VERSION = 0,
    SAMPLE_SYS_QUERY_NEXT_HEARTBEAT_MS = 1,
    SAMPLE_SYS_QUERY_TICKS_MS = 2,
};

int sample_sys_init(const RosKernelModuleApi* api) {
    if (!api || !api->logf) {
        return -1;
    }

    g_api = api;
    g_next_heartbeat_ms = 0;
    g_api->logf("INFO", "sample_sys", "sample module initialized");
    return 0;
}

void sample_sys_shutdown(void) {
    if (g_api && g_api->logf) {
        g_api->logf("INFO", "sample_sys", "sample module shutdown");
    }
}

void sample_sys_idle(uint64_t now_ms) {
    // if (!g_api || !g_api->logf) {
    //     return;
    // }
    // if (g_next_heartbeat_ms != 0 && now_ms < g_next_heartbeat_ms) {
    //     return;
    // }

    //g_api->logf("DEBUG", "sample_sys", "idle heartbeat at %llu ms", (unsigned long long)now_ms);
    // g_next_heartbeat_ms = now_ms + 5000ULL;

}

long sample_sys_query_status(unsigned long query, unsigned long unused) {
    (void)unused;

    if (!g_api) {
        return -1;
    }

    switch (query) {
    case SAMPLE_SYS_QUERY_ABI_VERSION:
        return (long)g_api->abi_version;
    case SAMPLE_SYS_QUERY_NEXT_HEARTBEAT_MS:
        return (long)g_next_heartbeat_ms;
    case SAMPLE_SYS_QUERY_TICKS_MS:
        return g_api->ticks_ms ? (long)g_api->ticks_ms() : -1;
    default:
        return -1;
    }
}

long do_add(unsigned long a, unsigned long b) {
    return (long)(a + b);
}

// return address of this module loaded in memory, or 0 if not found or on error
long sample_sys_get_loaded_address(void) {
    return (long)sample_sys_init;
}