/*
 * ldr_loader.h
 *
 * Public API for the modular loader.
 */
#ifndef LDR_LOADER_H
#define LDR_LOADER_H

#include "ldr_api.h"
#include "ldr_format.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LdrContextStruct LDR_CONTEXT;
    typedef struct LdrModuleStruct LDR_MODULE;

    typedef LDR_CONTEXT *PLDR_CONTEXT;
    typedef CONST LDR_CONTEXT *PCLDR_CONTEXT;
    typedef LDR_MODULE *PLDR_MODULE;
    typedef CONST LDR_MODULE *PCLDR_MODULE;

    typedef enum LdrResultEnum
    {
        LDR_OK = 0,
        LDR_E_INVALID_ARG,
        LDR_E_FORMAT,
        LDR_E_IO,
        LDR_E_OOM,
        LDR_E_VM,
        LDR_E_RELOC,
        LDR_E_IMPORT,
        LDR_E_STATE,
        LDR_E_NOT_FOUND
    } LDR_RESULT;

    typedef struct LdrLoadRequestStruct
    {
        CONST char *path;
        LDR_PROCESSHANDLE targetProcess;
        Flags flags;
    } LDR_LOADREQUEST;

    typedef LDR_LOADREQUEST *PLDR_LOADREQUEST;
    typedef CONST LDR_LOADREQUEST *PCLDR_LOADREQUEST;

    /*
     * Initialize loader context using kernel callback table.
     *
     * Args:
     *   api - pointer to populated kernel API table.
     *   out_ctx - receives initialized loader context.
     *
     * Returns:
     *   `LDR_OK` on success, error code otherwise.
     */
    LDR_RESULT ldr_init (PCLDR_KERNELAPI api, PLDR_CONTEXT *outContext);

    /*
     * Shutdown loader context and free all owned resources.
     *
     * Args:
     *   ctx - loader context previously returned by `ldr_init`.
     *
     * Returns:
     *   `LDR_OK` on success, `LDR_E_INVALID_ARG` if ctx is NULL.
     */
    LDR_RESULT ldr_shutdown (PLDR_CONTEXT context);

    /* Load executable image and prepare process launch state. */
    LDR_RESULT ldr_load_exe (PLDR_CONTEXT context, PCLDR_LOADREQUEST request, PLDR_MODULE *outModule);

    /* Load DLL image and resolve dependencies in target process. */
    LDR_RESULT ldr_load_dll (PLDR_CONTEXT context, PCLDR_LOADREQUEST request, PLDR_MODULE *outModule);

    /* Load kernel driver image and call driver init routine. */
    LDR_RESULT ldr_load_driver (PLDR_CONTEXT context, PCLDR_LOADREQUEST request, PLDR_MODULE *outModule);

    /* Unload module and run deinitialization path as needed. */
    LDR_RESULT ldr_unload_module (PLDR_CONTEXT context, PLDR_MODULE module);

    /* Lookup an exported symbol in one loaded module. */
    LDR_RESULT ldr_find_export (PLDR_CONTEXT context, PCLDR_MODULE module, CONST char *symbol, Address *outAddress);

    /* Call module init callback with module base as first argument. */
    LDR_RESULT ldr_call_init (PLDR_CONTEXT context, PLDR_MODULE module);

    /* Call module deinit callback with module base as first argument. */
    LDR_RESULT ldr_call_deinit (PLDR_CONTEXT context, PLDR_MODULE module);

#ifdef __cplusplus
}
#endif

#endif /* LDR_LOADER_H */
