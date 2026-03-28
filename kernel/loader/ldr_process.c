/*
 * ldr_process.c
 *
 * EXE launch preparation path.
 */
#include "../include/ldr_internal.h"

/*
 * ldr_process_prepare_exe
 *
 * Connect loaded EXE module to kernel process/task APIs.
 *
 * Args:
 *   ctx - loader context.
 *   module - loaded EXE module.
 *   req - load request carrying target process handle.
 *
 * Returns:
 *   LDR_OK on success, state error if process wiring fails.
 */
LDR_RESULT
ldr_process_prepare_exe(PLDR_CONTEXT context, PLDR_MODULE module, PCLDR_LOADREQUEST request) {
    Address entryAddress;
    Address stackTop;

    if (!context || !module || !request) {
        return LDR_E_INVALID_ARG;
    }
    if (module->kind != LDR_IMAGE_EXE) {
        return LDR_E_STATE;
    }

    /* Build entry program counter from module base + entry RVA. */
    entryAddress = module->baseAddress + module->entryRva;

    /* MVP stack policy: reserve top 64KB of module image as bootstrap stack marker. */
    stackTop = module->baseAddress + module->imageSize - 0x10000u;

    /* Register module and entrypoint in scheduler-visible process state. */
    if (context->api->procAddModule(request->targetProcess, module) != 0) {
        return LDR_E_STATE;
    }
    if (context->api->procSetEntry(request->targetProcess, entryAddress, stackTop) != 0) {
        return LDR_E_STATE;
    }

    return LDR_OK;
}
