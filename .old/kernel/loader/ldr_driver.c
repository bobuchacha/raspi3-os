/*
 * ldr_driver.c
 *
 * Driver-specific load finalization hook.
 */
#include "../include/ldr_internal.h"

 /*
  * ldr_driver_finalize_load
  *
  * Perform driver-only registration and policy checks after common load.
  *
  * Args:
  *   ctx - loader context.
  *   module - loaded driver module.
  *
  * Returns:
  *   LDR_OK on success, state error on policy failure.
  */
LDR_RESULT
ldr_driver_finalize_load(PLDR_CONTEXT context, PLDR_MODULE module) {
    if (!context || !module) {
        return LDR_E_INVALID_ARG;
    }
    if (module->kind != LDR_IMAGE_SYS) {
        return LDR_E_STATE;
    }

    /* Driver images may now run either in kernel space or user space. */
    return LDR_OK;
}
