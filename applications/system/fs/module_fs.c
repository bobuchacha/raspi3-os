#include "app/fs_module.h"
#include "app/kernel_module.h"

extern int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result);

static const RosKernelModuleApi* g_api;
static unsigned long g_backend_kind;
static unsigned long g_backend_features;
static const char* g_backend_module_name;

static const RosKernelModuleEmbeddedMetadata g_fs_metadata ROS_KERNEL_MODULE_METADATA_SECTION = {
    ROS_KERNEL_MODULE_EMBEDDED_MAGIC,
    ROS_KERNEL_MODULE_EMBEDDED_VERSION,
    0,
    "fs",
    "fs_module_init",
    "",
    "",
    2,
    {
        ROS_KERNEL_MODULE_EXPORT("query", fs_module_query_export),
        ROS_KERNEL_MODULE_EXPORT("call", fs_module_call_export),
    },
};

static int fs_detect_backend(void) {
    long result = 0;

    if (module_invoke("fat32", "probe", 0, 0, &result) == 0 && result > 0) {
        g_backend_kind = FS_MODULE_BACKEND_FAT32;
        g_backend_features = FS_MODULE_FEATURE_READ_FILE |
            FS_MODULE_FEATURE_LIST_DIR |
            FS_MODULE_FEATURE_WRITE_FILE |
            FS_MODULE_FEATURE_REMOVE;
        g_backend_module_name = "fat32";
        return 0;
    }

    g_backend_kind = FS_MODULE_BACKEND_NONE;
    g_backend_features = 0;
    g_backend_module_name = 0;
    return -1;
}

int fs_module_init(const RosKernelModuleApi* api) {
    g_api = api;
    if (fs_detect_backend() == 0) {
        if (g_api && g_api->logf) {
            g_api->logf("INFO", "fs", "selected FAT32 backend");
        }
        return 0;
    }

    if (g_api && g_api->logf) {
        g_api->logf("WARN", "fs", "no filesystem backend detected");
    }
    return 0;
}

long fs_module_query_export(unsigned long query, unsigned long unused) {
    (void)unused;

    if (query == FS_MODULE_QUERY_ABI_VERSION) {
        return ROS_KERNEL_MODULE_ABI_VERSION;
    }
    if (query == FS_MODULE_QUERY_BACKEND_KIND) {
        return (long)g_backend_kind;
    }
    if (query == FS_MODULE_QUERY_FEATURES) {
        return (long)g_backend_features;
    }
    return -1;
}

long fs_module_call_export(unsigned long operation, unsigned long request_ptr) {
    long result = -1;

    if (!g_backend_module_name && fs_detect_backend() != 0) {
        return -1;
    }
    if ((operation == FS_MODULE_OP_WRITE_FILE && (g_backend_features & FS_MODULE_FEATURE_WRITE_FILE) == 0) ||
        (operation == FS_MODULE_OP_MKDIR && (g_backend_features & FS_MODULE_FEATURE_MKDIR) == 0) ||
        (operation == FS_MODULE_OP_REMOVE && (g_backend_features & FS_MODULE_FEATURE_REMOVE) == 0)) {
        return -1;
    }
    if (module_invoke(g_backend_module_name, "call", operation, request_ptr, &result) != 0) {
        return -1;
    }
    return result;
}
