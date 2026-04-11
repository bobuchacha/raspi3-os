/*
 * ldr_loader.c
 *
 * High-level orchestration for executable, DLL, and driver load requests.
 *
 * This file owns the end-to-end load pipeline:
 * - read the packed image from VFS,
 * - parse metadata,
 * - reserve and commit runtime memory,
 * - copy sections and resolve relocations/imports,
 * - register the module in the global graph,
 * - and finally hand off to EXE or driver specific post-load paths.
 */
#include "../include/ldr_internal.h"
#include "log.h"
#include <string.h>

 /*
  * ldr_read_file
  *
  * Read an entire file through kernel VFS callbacks into heap memory.
  *
  * Args:
 *   context - active loader context.
  *   path - module path to read.
 *   outBuffer - receives allocated file buffer.
 *   outSize - receives total file size.
  *
  * Returns:
  *   LDR_OK on success, otherwise IO/OOM/invalid-arg error.
  */
static LDR_RESULT
ldr_read_file(PLDR_CONTEXT context, CONST char* path, Buffer* outBuffer, Size* outSize) {
    LDR_FILEHANDLE fileHandle = { 0 };
    ULong          fileSize = 0;
    Size           bytesRead = 0;
    Buffer         fileBuffer = NULL;

    if (!context || !path || !outBuffer || !outSize) {
        return LDR_E_INVALID_ARG;
    }

    /* Open file and query file length first so one allocation is enough. */
    if (context->api->vfsOpen(path, 0, &fileHandle) != 0) {
        return LDR_E_IO;
    }
    if (context->api->vfsSize(fileHandle, &fileSize) != 0 || fileSize == 0) {
        (void)context->api->vfsClose(fileHandle);
        return LDR_E_IO;
    }

    fileBuffer = (Buffer)context->api->heapAlloc(fileSize);
    if (!fileBuffer) {
        (void)context->api->vfsClose(fileHandle);
        return LDR_E_OOM;
    }

    /* Read exact byte count expected by file-size query. */
    if (context->api->vfsReadAt(fileHandle, 0, fileBuffer, fileSize, &bytesRead) != 0 || bytesRead != fileSize) {
        context->api->heapFree(fileBuffer);
        (void)context->api->vfsClose(fileHandle);
        return LDR_E_IO;
    }

    (void)context->api->vfsClose(fileHandle);
    *outBuffer = fileBuffer;
    *outSize = fileSize;
    return LDR_OK;
}

/*
 * Release every allocation associated with a partially constructed module after
 * any stage of the common load pipeline fails.
 */
static void
ldr_dispose_failed_module(PLDR_CONTEXT context, PLDR_MODULE module) {
    UInt itemIndex;

    if (!context || !module) {
        return;
    }

    if (module->baseAddress != 0 && module->imageSize != 0) {
        (void)context->api->vmRelease(module->baseAddress, module->imageSize);
    }

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
    if (module->dependencies) {
        for (itemIndex = 0; itemIndex < module->dependencyCount; ++itemIndex) {
            if (module->dependencies[itemIndex]) {
                (void)ldr_release_module(context, module->dependencies[itemIndex]);
            }
        }
        context->api->heapFree(module->dependencies);
    }
    if (module->path) {
        context->api->heapFree((Pointer)module->path);
    }

    context->api->heapFree(module);
}

/*
 * ldr_common_load
 *
 * Shared load pipeline for EXE/DLL/SYS artifacts.
 *
 * Args:
 *   context - active loader context.
 *   request - load request with path and target process.
 *   expectedKind - expected image kind for this path.
 *   outModule - receives loaded module handle.
 *
 * Returns:
 *   LDR_OK on success, otherwise parser/memory/import/reloc error.
 */
static LDR_RESULT
ldr_common_load(
    PLDR_CONTEXT      context,
    PCLDR_LOADREQUEST request,
    LDR_IMAGEKIND     expectedKind,
    PLDR_MODULE* outModule) {
    LDR_RESULT  result;
    Buffer      imageBuffer = NULL;
    Size        imageSize = 0;
    PLDR_MODULE module = NULL;

    if (!context || !request || !request->path || !outModule) {
        return LDR_E_INVALID_ARG;
    }

    /* Read artifact bytes so parser and section copier can operate. */
    result = ldr_read_file(context, request->path, &imageBuffer, &imageSize);
    if (result != LDR_OK) {
        return result;
    }

    /* Parse structural metadata from the container header and tables. */
    result = ldr_parse_image(context, imageBuffer, imageSize, request->path, &module);
    if (result != LDR_OK) {
        context->api->heapFree(imageBuffer);
        return result;
    }

    /* Validate requested load API against encoded image kind. */
    if (module->kind != expectedKind) {
        ldr_dispose_failed_module(context, module);
        context->api->heapFree(imageBuffer);
        return LDR_E_FORMAT;
    }

    module->ownerProcess = request->targetProcess;
    module->isKernelModule = ((request->flags & LDR_VM_KERN) != 0);
    module->referenceCount = 1;

    /* Reserve+commit VM region for final mapped image. */
    result = ldr_vm_map_image(context, module);
    if (result != LDR_OK) {
        if (context->api->logPrintf) {
            context->api->logPrintf(0, "ldr_common_load: vm map failed for %s (result=%d)", request->path, result);
        }
        ldr_dispose_failed_module(context, module);
        context->api->heapFree(imageBuffer);
        return result;
    }

    /* Copy or zero-fill all sections from file into mapped image memory. */
    result = ldr_sections_load(context, module, imageBuffer, imageSize);
    if (result != LDR_OK) {
        if (context->api->logPrintf) {
            context->api->logPrintf(0, "ldr_common_load: section load failed for %s (result=%d)", request->path, result);
        }
        ldr_dispose_failed_module(context, module);
        context->api->heapFree(imageBuffer);
        return result;
    }

    /* Resolve imports first so relocation can patch final callsites. */
    result = ldr_imports_bind(context, module);
    if (result != LDR_OK) {
        if (context->api->logPrintf) {
            context->api->logPrintf(0, "ldr_common_load: import bind failed for %s (result=%d)", request->path, result);
        }
        ldr_dispose_failed_module(context, module);
        context->api->heapFree(imageBuffer);
        return result;
    }

    /* Apply relocations relative to runtime base selected by VM layer. */
    result = ldr_reloc_apply(context, module);
    if (result != LDR_OK) {
        if (context->api->logPrintf) {
            context->api->logPrintf(0, "ldr_common_load: reloc apply failed for %s (result=%d)", request->path, result);
        }
        ldr_dispose_failed_module(context, module);
        context->api->heapFree(imageBuffer);
        return result;
    }

    /* Track module in global graph before invoking any init callbacks. */
    result = ldr_module_register(context, module);
    if (result != LDR_OK) {
        if (context->api->logPrintf) {
            context->api->logPrintf(0, "ldr_common_load: module register failed for %s (result=%d)", request->path, result);
        }
        ldr_dispose_failed_module(context, module);
        context->api->heapFree(imageBuffer);
        return result;
    }

    /*
     * Only kernel-space modules may execute `Init` directly from the loader.
     * User-space DLL/SYS images must run `Init` later from EL0 after the
     * syscall returns, otherwise the kernel would branch into user code while
     * still executing in EL1.
     */
    if (expectedKind != LDR_IMAGE_EXE && module->isKernelModule) {
        result = ldr_call_init(context, module);
        if (result != LDR_OK) {
            (void)ldr_unload_module(context, module);
            context->api->heapFree(imageBuffer);
            return result;
        }
    }

    context->api->heapFree(imageBuffer);
    *outModule = module;
    return LDR_OK;
}

LDR_RESULT
ldr_init(PCLDR_KERNELAPI api, PLDR_CONTEXT* outContext) {
    PLDR_CONTEXT context;

    log_info("Initialize loader");

    if (!api || !outContext) {
        return LDR_E_INVALID_ARG;
    }

    /* Validate required callbacks before we start using the context. */
    if (!api->vmReserve || !api->vmCommit || !api->vmProtect || !api->vmRelease || !api->heapAlloc || !api->heapFree || !api->vfsOpen || !api->vfsReadAt || !api->vfsSize || !api->vfsClose || !api->lockCreate || !api->lockAcquire || !api->lockRelease) {
        return LDR_E_INVALID_ARG;
    }

    context = (PLDR_CONTEXT)api->heapAlloc((Size)sizeof(*context));
    if (!context) {
        return LDR_E_OOM;
    }

    memset(context, 0, sizeof(*context));
    context->api = api;

    if (context->api->lockCreate(&context->graphLock) != 0) {
        context->api->heapFree(context);
        return LDR_E_STATE;
    }

    *outContext = context;
    return LDR_OK;
}

/*
 * Shut down the loader by forcibly unloading every tracked module and freeing
 * the context itself.
 */
LDR_RESULT
ldr_shutdown(PLDR_CONTEXT context) {
    if (!context) {
        return LDR_E_INVALID_ARG;
    }

    /*
     * Always unload the current head again after each iteration because one
     * forced unload may recursively release and destroy additional dependency
     * modules deeper in the list.
     */
    while (context->moduleHead) {
        (void)ldr_unload_module_force(context, context->moduleHead);
    }

    context->api->heapFree(context);
    return LDR_OK;
}

/* Load one executable image and wire its entry state into the target process. */
LDR_RESULT
ldr_load_exe(PLDR_CONTEXT context, PCLDR_LOADREQUEST request, PLDR_MODULE* outModule) {
    LDR_RESULT result = ldr_common_load(context, request, LDR_IMAGE_EXE, outModule);
    if (result == LDR_OK) {
        /* Process bootstrap is only meaningful for EXE load path. */
        result = ldr_process_prepare_exe(context, *outModule, request);
    }
    return result;
}

/* Load one DLL image through the common module pipeline. */
LDR_RESULT
ldr_load_dll(PLDR_CONTEXT context, PCLDR_LOADREQUEST request, PLDR_MODULE* outModule) {
    return ldr_common_load(context, request, LDR_IMAGE_DLL, outModule);
}

/* Load one driver image and run the driver-specific finalize hook. */
LDR_RESULT
ldr_load_driver(PLDR_CONTEXT context, PCLDR_LOADREQUEST request, PLDR_MODULE* outModule) {
    LDR_RESULT result = ldr_common_load(context, request, LDR_IMAGE_SYS, outModule);
    if (result == LDR_OK) {
        /* Final kernel-driver registration can include privileged bookkeeping. */
        result = ldr_driver_finalize_load(context, *outModule);
    }
    return result;
}
