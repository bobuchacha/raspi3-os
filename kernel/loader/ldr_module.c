/*
 * ldr_module.c
 *
 * Module registry, init/deinit callbacks, and unload logic.
 */
#include "../include/ldr_internal.h"

/*
 * ldr_call_entry
 *
 * Invoke module callback that expects first arg as module base.
 *
 * Args:
 *   module - module owning callback address.
 *   entry - absolute callback function address.
 *
 * Returns:
 *   0 from callback on success, non-zero callback code otherwise.
 */
static Int
ldr_call_entry(PLDR_MODULE module, Address entryAddress) {
    typedef Int (*PLDR_ENTRYFUNCTION)(Pointer baseAddress);
    PLDR_ENTRYFUNCTION entryFunction = (PLDR_ENTRYFUNCTION)entryAddress;
    return entryFunction((Pointer)module->baseAddress);
}

/*
 * ldr_module_register
 *
 * Insert module into context list while holding graph lock.
 *
 * Args:
 *   ctx - loader context.
 *   module - module to register.
 *
 * Returns:
 *   LDR_OK on success, state/arg errors otherwise.
 */
LDR_RESULT
ldr_module_register(PLDR_CONTEXT context, PLDR_MODULE module) {
    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }

    if (context->api->lockAcquire(context->graphLock) != 0) {
        return LDR_E_STATE;
    }

    /* Push-front list insert keeps operation O(1) and deterministic. */
    module->nextModule = context->moduleHead;
    context->moduleHead = module;

    (void)context->api->lockRelease(context->graphLock);
    return LDR_OK;
}

/*
 * ldr_module_unregister
 *
 * Remove module from global list while holding graph lock.
 *
 * Args:
 *   ctx - loader context.
 *   module - module to remove.
 *
 * Returns:
 *   LDR_OK when removed, not-found/state error otherwise.
 */
LDR_RESULT
ldr_module_unregister(PLDR_CONTEXT context, PLDR_MODULE module) {
    PLDR_MODULE *moduleLink;

    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }

    if (context->api->lockAcquire(context->graphLock) != 0) {
        return LDR_E_STATE;
    }

    /* Pointer-to-pointer traversal allows unlink without prev temporary. */
    for (moduleLink = &context->moduleHead; *moduleLink; moduleLink = &(*moduleLink)->nextModule) {
        if (*moduleLink == module) {
            *moduleLink = module->nextModule;
            (void)context->api->lockRelease(context->graphLock);
            return LDR_OK;
        }
    }

    (void)context->api->lockRelease(context->graphLock);
    return LDR_E_NOT_FOUND;
}

/*
 * ldr_call_init
 *
 * Call module init entrypoint (`entry_rva`) with base pointer.
 *
 * Args:
 *   ctx - loader context for optional diagnostics.
 *   module - target module.
 *
 * Returns:
 *   LDR_OK when init succeeds or no init exists; state error on failure.
 */
LDR_RESULT
ldr_call_init(PLDR_CONTEXT context, PLDR_MODULE module) {
    Address entryAddress;
    Int     callbackResult;

    (void)context;
    if (!module) {
        return LDR_E_INVALID_ARG;
    }
    if (module->initCalled) {
        return LDR_OK;
    }
    if (module->entryRva == 0) {
        module->initCalled = true;
        return LDR_OK;
    }

    entryAddress = module->baseAddress + module->entryRva;
    callbackResult = ldr_call_entry(module, entryAddress);
    if (callbackResult != 0) {
        return LDR_E_STATE;
    }

    module->initCalled = true;
    return LDR_OK;
}

/*
 * ldr_call_deinit
 *
 * Call module deinit callback if exported as `Deinit` symbol.
 *
 * Args:
 *   ctx - loader context.
 *   module - target module.
 *
 * Returns:
 *   LDR_OK on success or if no callback exists.
 */
LDR_RESULT
ldr_call_deinit(PLDR_CONTEXT context, PLDR_MODULE module) {
    Address deinitAddress = 0;

    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }
    if (module->deinitCalled) {
        return LDR_OK;
    }

    /* Deinit callback is optional; absence is not considered an error. */
    if (ldr_find_export(context, module, "Deinit", &deinitAddress) == LDR_OK) {
        if (ldr_call_entry(module, deinitAddress) != 0) {
            return LDR_E_STATE;
        }
    }

    module->deinitCalled = true;
    return LDR_OK;
}

/*
 * ldr_unload_module
 *
 * Deinitialize module, untrack it, and release VM + heap allocations.
 *
 * Args:
 *   ctx - loader context.
 *   module - module to unload.
 *
 * Returns:
 *   LDR_OK on full cleanup, error code on failure.
 */
LDR_RESULT
ldr_unload_module(PLDR_CONTEXT context, PLDR_MODULE module) {
    UInt itemIndex;

    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }

    /* Best-effort deinit first so module can release owned resources. */
    if (ldr_call_deinit(context, module) != LDR_OK) {
        return LDR_E_STATE;
    }

    (void)ldr_module_unregister(context, module);

    if (module->baseAddress != 0 && module->imageSize != 0) {
        if (context->api->vmRelease(module->baseAddress, module->imageSize) != 0) {
            return LDR_E_VM;
        }
    }

    /* Free all dynamically duplicated strings and metadata arrays. */
    for (itemIndex = 0; itemIndex < module->sectionCount; ++itemIndex) {
        if (module->sections[itemIndex].name) {
            context->api->heapFree((Pointer)module->sections[itemIndex].name);
        }
    }
    for (itemIndex = 0; itemIndex < module->importCount; ++itemIndex) {
        if (module->imports[itemIndex].moduleName) {
            context->api->heapFree((Pointer)module->imports[itemIndex].moduleName);
        }
        if (module->imports[itemIndex].symbolName) {
            context->api->heapFree((Pointer)module->imports[itemIndex].symbolName);
        }
    }
    for (itemIndex = 0; itemIndex < module->exportCount; ++itemIndex) {
        if (module->exports[itemIndex].symbolName) {
            context->api->heapFree((Pointer)module->exports[itemIndex].symbolName);
        }
    }

    if (module->sections) {
        context->api->heapFree(module->sections);
    }
    if (module->imports) {
        context->api->heapFree(module->imports);
    }
    if (module->exports) {
        context->api->heapFree(module->exports);
    }
    if (module->relocs) {
        context->api->heapFree(module->relocs);
    }
    if (module->path) {
        context->api->heapFree((Pointer)module->path);
    }

    context->api->heapFree(module);
    return LDR_OK;
}
