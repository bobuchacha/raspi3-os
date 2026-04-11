/*
 * ldr_imports.c
 *
 * Import resolution and dependency tracking for loaded modules.
 *
 * The binder resolves each import against already-loaded modules, patches the
 * import address table, and records retained dependency edges so unload can
 * later release them exactly once.
 */
#include "../include/ldr_internal.h"

#include <string.h>

 /*
  * ldr_find_module_by_path_suffix
  *
  * Lookup loaded module by basename/suffix match.
  *
  * Args:
 *   context - loader context.
 *   moduleName - module name referenced by one import record.
  *
  * Returns:
  *   Matching module or NULL when not found.
  */
static PLDR_MODULE
ldr_find_module_by_path_suffix(PLDR_CONTEXT context, CONST char* moduleName) {
    PLDR_MODULE currentModule;
    Size wantedLength;

    if (!context || !moduleName) {
        return NULL;
    }

    wantedLength = (Size)strlen(moduleName);
    for (currentModule = context->moduleHead; currentModule; currentModule = currentModule->nextModule) {
        Size currentLength;

        if (!currentModule->path) {
            continue;
        }
        currentLength = (Size)strlen(currentModule->path);

        /* Suffix compare allows importing by basename even with absolute paths. */
        if (currentLength >= wantedLength && strcmp(currentModule->path + (currentLength - wantedLength), moduleName) == 0) {
            return currentModule;
        }
    }

    return NULL;
}

/*
 * ldr_find_module_by_path
 *
 * Public resolver used by the runtime to look up one loaded module by either
 * its full stored path or the basename/suffix form used by import records.
 */
LDR_RESULT
ldr_find_module_by_path(PLDR_CONTEXT context, CONST char* path, PLDR_MODULE* outModule) {
    PLDR_MODULE module;

    if (!context || !path || !outModule) {
        return LDR_E_INVALID_ARG;
    }

    module = ldr_find_module_by_path_suffix(context, path);
    if (!module) {
        return LDR_E_NOT_FOUND;
    }

    *outModule = module;
    return LDR_OK;
}

/*
 * ldr_find_export_in_module
 *
 * Resolve one symbol within one module export table.
 *
 * Args:
 *   module - exporting module.
 *   symbolName - symbol string to resolve.
 *   outAddress - receives absolute runtime address.
 *
 * Returns:
 *   LDR_OK when resolved, LDR_E_NOT_FOUND otherwise.
 */
static LDR_RESULT
ldr_find_export_in_module(PCLDR_MODULE module, CONST char* symbolName, Address* outAddress) {
    UInt exportIndex;

    if (!module || !symbolName || !outAddress) {
        /*
         * Retain one dependency edge the first time a module imports anything from a
         * given provider module.
         */
        return LDR_E_INVALID_ARG;
    }

    for (exportIndex = 0; exportIndex < module->exportCount; ++exportIndex) {
        if (module->exports[exportIndex].symbolName && strcmp(module->exports[exportIndex].symbolName, symbolName) == 0) {
            *outAddress = module->baseAddress + module->exports[exportIndex].symbolRva;
            return LDR_OK;
        }
    }

    return LDR_E_NOT_FOUND;
}

/*
 * ldr_record_dependency
 *
 * Remember one unique module dependency so unload can later release it once.
 */
static LDR_RESULT
ldr_record_dependency(PLDR_CONTEXT context, PLDR_MODULE module, PLDR_MODULE dependency) {
    PLDR_MODULE* newDependencies;

    if (!context || !module || !dependency || module == dependency) {
        return LDR_E_INVALID_ARG;
    }

    for (UInt index = 0; index < module->dependencyCount; ++index) {
        if (module->dependencies[index] == dependency) {
            return LDR_OK;
        }
    }

    if (module->dependencyCapacity == 0) {
        module->dependencyCapacity = module->importCount ? module->importCount : 1u;
        newDependencies = (PLDR_MODULE*)context->api->heapAlloc(sizeof(PLDR_MODULE) * module->dependencyCapacity);
        if (!newDependencies) {
            module->dependencyCapacity = 0;
            return LDR_E_OOM;
        }

        memset(newDependencies, 0, sizeof(PLDR_MODULE) * module->dependencyCapacity);
        module->dependencies = newDependencies;
    }

    if (module->dependencyCount >= module->dependencyCapacity) {
        return LDR_E_STATE;
    }

    module->dependencies[module->dependencyCount++] = dependency;
    if (ldr_retain_module(context, dependency) != LDR_OK) {
        module->dependencyCount--;
        module->dependencies[module->dependencyCount] = 0;
        return LDR_E_STATE;
    }

    return LDR_OK;
}

/*
 * ldr_imports_bind
 *
 * Fill import address table entries using dependency export symbols.
 *
 * Args:
 *   context - loader context with module graph.
 *   module - module that owns unresolved imports.
 *
 * Returns:
 *   LDR_OK on success, `LDR_E_IMPORT` when any symbol is unresolved.
 */
LDR_RESULT
ldr_imports_bind(PLDR_CONTEXT context, PLDR_MODULE module) {
    UInt importIndex;

    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }

    /* Resolve each import independently to keep failure reporting precise. */
    for (importIndex = 0; importIndex < module->importCount; ++importIndex) {
        PLDR_IMPORT importItem = &module->imports[importIndex];
        PLDR_MODULE dependency;
        Address functionAddress = 0;
        dependency = ldr_find_module_by_path_suffix(context, importItem->moduleName);
        if (!dependency) {
            return LDR_E_IMPORT;
        }
        if (ldr_record_dependency(context, module, dependency) != LDR_OK) {
            return LDR_E_IMPORT;
        }

        if (ldr_find_export_in_module(dependency, importItem->symbolName, &functionAddress) != LDR_OK) {
            return LDR_E_IMPORT;
        }

        /* Write resolved absolute function address into target IAT slot. */
        if (importItem->iatRva + sizeof(ULong) > module->imageSize) {
            return LDR_E_FORMAT;
        }
        if (ldr_write_module_u64(module, module->baseAddress + importItem->iatRva, functionAddress) != 0) {
            return LDR_E_VM;
        }
    }

    return LDR_OK;
}

LDR_RESULT
ldr_find_export(PLDR_CONTEXT context, PCLDR_MODULE module, CONST char* symbolName, Address* outAddress) {
    (void)context;
    return ldr_find_export_in_module(module, symbolName, outAddress);
}
