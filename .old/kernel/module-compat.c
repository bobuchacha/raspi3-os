#include "filesystem/vfs/vfs.h"
#include "gui.h"
#include "input.h"
#include "log.h"
#include "module.h"
#include "touch.h"
#include "utils.h"

/* Compatibility flag for the legacy module runtime shim. */
static Bool g_module_subsystem_ready = false;

static int module_dispatch_builtin(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    if (!module_name || !export_name || !result) {
        return -1;
    }

    if (strcmp(module_name, "fbgui") == 0) {
        if (strcmp(export_name, "push_key") == 0) {
            *result = gui_key(a, b);
            return 0;
        }
        if (strcmp(export_name, "push_pointer") == 0) {
            *result = gui_pointer(a, b);
            return 0;
        }
        if (strcmp(export_name, "usb_enabled") == 0) {
            *result = gui_usb_enabled(a, b);
            return 0;
        }
        return -1;
    }

    if (strcmp(module_name, "usbkey") == 0 && strcmp(export_name, "inject_key") == 0) {
        if (input_enqueue_key(a) != 0) {
            *result = -1;
            return -1;
        }

        *result = gui_key(a, b);
        return 0;
    }

    if (strcmp(module_name, "usbmouse") == 0 && strcmp(export_name, "inject_pointer") == 0) {
        const TouchState* touch_state = touch_get_state();
        unsigned long pressed = touch_state && touch_state->pressed ? 1UL : 0UL;

        *result = gui_pointer_state(a, b, pressed);
        return 0;
    }

    return -1;
}

/*
 * Bring the legacy module compatibility layer into a known initialized state.
 */
void module_subsystem_init(void) {
    g_module_subsystem_ready = true;
    log_info("Module compatibility layer ready");
}

/*
 * Preserve the historical boot-module hook even while the old runtime is
 * intentionally stubbed out.
 */
void module_load_boot_modules(void) {
    if (!g_module_subsystem_ready) {
        module_subsystem_init();
    }
    log_info("Skipping boot module scan; legacy module runtime is disabled");
}

/*
 * Placeholder idle-loop hook for compatibility callers.
 */
void module_run_idle_loops(void) {
}

/*
 * Route a small set of built-in compatibility exports while the full legacy
 * module runtime stays disabled.
 */
int module_invoke(const char* module_name, const char* export_name, unsigned long a, unsigned long b, long* result) {
    // _trace("module_invoke: module=%s export=%s a=0x%lX b=0x%lX", module_name, export_name, a, b);
    if (result) {
        *result = -1;
    }

    if (module_dispatch_builtin(module_name, export_name, a, b, result) == 0) {
        return 0;
    }

    return -1;
}

/*
 * Read the fixed-size bundle header from the start of one module file.
 */
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

/*
 * Validate the magic bytes and version fields for one packed module bundle.
 */
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

/* Return no module because the compatibility backend does not track modules. */
const KernelModuleInfo* module_find(const char* name) {
    (void)name;
    return null;
}

/* Return no indexed module because the compatibility backend is empty. */
const KernelModuleInfo* module_get_at(unsigned int index) {
    (void)index;
    return null;
}

/* Report zero live compatibility modules. */
unsigned int module_count(void) {
    return 0;
}

/* Report zero exports because no compatibility modules are loaded. */
unsigned int module_export_count(const char* name) {
    (void)name;
    return 0;
}

/* Clear the caller buffer and fail because no compatibility exports exist. */
int module_export_get(const char* name, unsigned int index, KernelModuleExportInfo* info) {
    (void)name;
    (void)index;
    if (info) {
        memzero((Address)info, sizeof(*info));
    }
    return -1;
}

/* Clear the caller buffer and fail because no compatibility runtime exists. */
int module_runtime_get(const char* name, KernelModuleRuntimeInfo* runtime) {
    (void)name;
    if (runtime) {
        memzero((Address)runtime, sizeof(*runtime));
    }
    return -1;
}