#include "log.h"
#include "module.h"
#include "utils.h"

static KernelModuleInfo module_infos[MODULE_MAX_COUNT];

/**
 * Reset the in-memory registry for kernel module metadata.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Clears all registry slots and logs the reserved virtual address window used
 *   by module runtimes.
 *
 * Returns:
 *   Nothing.
 */
void module_subsystem_init(void) {
    memzero((Address)module_infos, sizeof(module_infos));
    log_info("Module subsystem ready: region [0x%lX, 0x%lX)", MODULE_REGION_BASE, MODULE_REGION_LIMIT);
}

/**
 * Reserve a free registry slot for a module discovered on disk.
 *
 * Args:
 *   name: Display name to store for the module.
 *   path: Filesystem path that produced the module.
 *
 * Behavior:
 *   Scans the fixed-size registry, clears the first free slot, marks it as
 *   discovered, and copies the provided identifiers.
 *
 * Returns:
 *   Pointer to the reserved slot on success, or `null` when the registry is
 *   full.
 */
KernelModuleInfo* module_registry_reserve(const char* name, const char* path) {
    for (UInt index = 0; index < MODULE_MAX_COUNT; index++) {
        if (module_infos[index].used) {
            continue;
        }

        // Reinitialize the slot so stale metadata never leaks between loads.
        memzero((Address)&module_infos[index], sizeof(module_infos[index]));
        module_infos[index].used = true;
        module_infos[index].state = MODULE_STATE_DISCOVERED;
        if (name) {
            strncpy(module_infos[index].name, name, sizeof(module_infos[index].name) - 1);
        }
        if (path) {
            strncpy(module_infos[index].path, path, sizeof(module_infos[index].path) - 1);
        }
        return &module_infos[index];
    }

    return null;
}

/**
 * Mark a reserved registry slot as failed.
 *
 * Args:
 *   info: Registry entry to update.
 *
 * Behavior:
 *   Leaves all descriptive metadata intact and only flips the state so callers
 *   can inspect what failed later.
 *
 * Returns:
 *   Nothing.
 */
void module_registry_mark_failed(KernelModuleInfo* info) {
    if (!info) {
        return;
    }
    info->state = MODULE_STATE_FAILED;
}

/**
 * Mark a registry slot as ready and persist loaded-image statistics.
 *
 * Args:
 *   info: Registry entry to update.
 *   flags: Module flags copied from the bundle header.
 *   image_size: Size of the mapped runtime image.
 *   bss_size: Size of the zero-filled BSS region.
 *
 * Behavior:
 *   Copies runtime bookkeeping into the registry entry and transitions it to
 *   the ready state.
 *
 * Returns:
 *   Nothing.
 */
void module_registry_mark_ready(KernelModuleInfo* info, unsigned int flags, unsigned long image_size, unsigned long bss_size) {
    if (!info) {
        return;
    }
    info->flags = flags;
    info->image_size = image_size;
    info->bss_size = bss_size;
    info->state = MODULE_STATE_READY;
}

/**
 * Find a module registry entry by name.
 *
 * Args:
 *   name: Module name to search for.
 *
 * Behavior:
 *   Performs a linear scan over the registry and compares stored names against
 *   the requested name.
 *
 * Returns:
 *   Pointer to the matching registry entry, or `null` when no match exists.
 */
const KernelModuleInfo* module_find(const char* name) {
    if (!name || name[0] == '\0') {
        return null;
    }

    for (UInt index = 0; index < MODULE_MAX_COUNT; index++) {
        if (!module_infos[index].used) {
            continue;
        }
        if (strncmp(module_infos[index].name, name, sizeof(module_infos[index].name)) == 0) {
            return &module_infos[index];
        }
    }

    return null;
}

/**
 * Fetch one occupied registry entry by ordinal index.
 *
 * Args:
 *   index: Zero-based ordinal across used registry entries.
 *
 * Behavior:
 *   Skips unused slots and returns the `index`-th occupied entry so callers do
 *   not depend on the registry's sparse internal layout.
 *
 * Returns:
 *   Pointer to the requested registry entry, or `null` when out of range.
 */
const KernelModuleInfo* module_get_at(unsigned int index) {
    unsigned int ordinal = 0;

    for (UInt slot = 0; slot < MODULE_MAX_COUNT; slot++) {
        if (!module_infos[slot].used) {
            continue;
        }
        if (ordinal == index) {
            return &module_infos[slot];
        }
        ordinal++;
    }

    return null;
}

/**
 * Count the number of occupied registry slots.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Counts every slot currently marked as used, regardless of whether the
 *   module is discovered, ready, or failed.
 *
 * Returns:
 *   Number of occupied registry entries.
 */
unsigned int module_count(void) {
    unsigned int count = 0;

    for (UInt index = 0; index < MODULE_MAX_COUNT; index++) {
        if (module_infos[index].used) {
            count++;
        }
    }
    return count;
}