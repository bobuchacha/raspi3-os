#include "filesystem/vfs/vfs.h"
#include "log.h"
#include "module.h"
#include "utils.h"

static Bool g_module_subsystem_ready = false;

void module_subsystem_init(void) {
    g_module_subsystem_ready = true;
    log_info("Module compatibility layer ready");
}

void module_load_boot_modules(void) {
    if (!g_module_subsystem_ready) {
        module_subsystem_init();
    }
    log_info("Skipping boot module scan; legacy module runtime is disabled");
}

void module_run_idle_loops(void) {
}

int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    (void)module_name;
    (void)export_name;
    (void)a;
    (void)b;
    if (result) {
        *result = -1;
    }
    return -1;
}

int module_read_header(struct FileDesc* fd, ModuleBundleHeader* header) {
    if (!fd || !header) {
        return -1;
    }
    if (vfs_fd_seek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    if (vfs_fd_read(fd, header, sizeof(*header)) != (int)sizeof(*header)) {
        return -1;
    }
    return 0;
}

int module_validate_header(const ModuleBundleHeader* header) {
    if (!header) {
        return -1;
    }
    if (header->magic[0] != MODULE_BUNDLE_MAGIC_0 ||
        header->magic[1] != MODULE_BUNDLE_MAGIC_1 ||
        header->magic[2] != MODULE_BUNDLE_MAGIC_2 ||
        header->magic[3] != MODULE_BUNDLE_MAGIC_3 ||
        header->magic[4] != MODULE_BUNDLE_MAGIC_4 ||
        header->magic[5] != MODULE_BUNDLE_MAGIC_5 ||
        header->magic[6] != MODULE_BUNDLE_MAGIC_6 ||
        header->magic[7] != MODULE_BUNDLE_MAGIC_7) {
        return -1;
    }
    if (header->header_size < MODULE_BUNDLE_HEADER_SIZE || header->version != MODULE_BUNDLE_VERSION) {
        return -1;
    }
    return 0;
}

const KernelModuleInfo* module_find(const char* name) {
    (void)name;
    return null;
}

const KernelModuleInfo* module_get_at(unsigned int index) {
    (void)index;
    return null;
}

unsigned int module_count(void) {
    return 0;
}

unsigned int module_export_count(const char* name) {
    (void)name;
    return 0;
}

int module_export_get(const char* name, unsigned int index, KernelModuleExportInfo* info) {
    (void)name;
    (void)index;
    if (info) {
        memzero((Address)info, sizeof(*info));
    }
    return -1;
}

int module_runtime_get(const char* name, KernelModuleRuntimeInfo* runtime) {
    (void)name;
    if (runtime) {
        memzero((Address)runtime, sizeof(*runtime));
    }
    return -1;
}