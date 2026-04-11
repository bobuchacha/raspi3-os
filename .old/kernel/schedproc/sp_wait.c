/*
 * sp_wait.c
 *
 * Placeholder wait subsystem file. Full wait-node and synchronization-object
 * support will land in the next implementation phases.
 */
#include "../include/sp_wait.h"

 /*
  * Report the current wait-subsystem implementation status.
  *
  * This helper is intentionally side-effect free because callers often use it
  * while building diagnostic log lines. Emitting a nested log message here would
  * recurse into the console lock and can deadlock the kernel logger.
  */
SP_RESULT
sp_wait_feature_status(void) {
    return SP_E_NOT_IMPLEMENTED;
}
