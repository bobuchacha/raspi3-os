#include "log.h"

/**
 * Emit a placeholder status message for the module relocation subsystem.
 *
 * Args:
 *   None.
 *
 * Behavior:
 *   Logs that the standalone relocation subsystem remains unimplemented.
 *
 * Returns:
 *   Nothing.
 */
void module_reloc_subsystem_note(void) {
    log_info("Module relocation engine pending implementation");
}