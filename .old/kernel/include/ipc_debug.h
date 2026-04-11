/*
 * ipc_debug.h
 *
 * File purpose:
 *   Diagnostics surface for rendering the module state in a readable text form.
 *
 * Procedure call overview:
 *   Call ipc_dump_state() from a smoke test, debugger command, or kernel trace
 *   hook when you want a compact view of registered events, mappings, and
 *   mailboxes.
 */
#ifndef IPC_DEBUG_H
#define IPC_DEBUG_H

#include "ipc_mailbox.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write a multi-section snapshot of the IPC context into the supplied stream. */
void ipc_dump_state(PCIPC_CONTEXT context, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* IPC_DEBUG_H */
