/*
 * ipc_module.h
 *
 * File purpose:
 *   Declares top-level lifetime functions for creating and destroying an IPC
 *   module instance.
 *
 * Procedure call overview:
 *   1. ipc_init() creates the module context and captures kernel callbacks.
 *   2. Callers create/open IPC objects through the other public headers.
 *   3. ipc_shutdown() tears down any remaining objects owned by the context.
 */
#ifndef IPC_MODULE_H
#define IPC_MODULE_H

#include "ipc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create one IPC module context.
 *
 * Parameters:
 *   kernelApi   Optional callback table for allocation, time, and logging.
 *   outContext  Receives the created module context on success.
 */
IPC_RESULT ipc_init(PCIPC_KERNELAPI kernelApi, PIPC_CONTEXT *outContext);

/*
 * Destroy an IPC module context and all objects still linked under it.
 *
 * This is a shutdown helper, not a reference-safe runtime garbage collector.
 * The current scaffold assumes tests and early kernel integration own the full
 * module lifetime.
 */
IPC_RESULT ipc_shutdown(PIPC_CONTEXT context);

#ifdef __cplusplus
}
#endif

#endif /* IPC_MODULE_H */
