/*
 * ipc_mailbox.h
 *
 * File purpose:
 *   Public API for named mailbox queues.
 *
 * Design notes:
 *   The mailbox is the module's first queued-message primitive. It uses a
 *   bounded ring buffer because that gives deterministic memory usage and a
 *   simple enqueue/dequeue algorithm suitable for early kernel integration.
 */
#ifndef IPC_MAILBOX_H
#define IPC_MAILBOX_H

#include "ipc_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a named mailbox with fixed-size slots and fixed queue depth. */
IPC_RESULT ipc_create_mailbox(PIPC_CONTEXT context, const char *name, size_t slotSize, size_t slotCount, PIPC_MAILBOX *outMailbox);
/* Open an existing named mailbox and acquire another reference. */
IPC_RESULT ipc_open_mailbox(PIPC_CONTEXT context, const char *name, PIPC_MAILBOX *outMailbox);
/* Release one mailbox reference and destroy it when no references remain. */
IPC_RESULT ipc_close_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox);
/* Enqueue one message into the mailbox ring. */
IPC_RESULT ipc_send_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox, const void *message, size_t length);
/* Dequeue one message from the mailbox ring into a caller-provided buffer. */
IPC_RESULT ipc_receive_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox, void *buffer, size_t bufferSize, size_t *outLength);
/* Report current mailbox occupancy and configuration. */
IPC_RESULT ipc_query_mailbox(PIPC_MAILBOX mailbox, IPC_MAILBOXINFO *outInfo);

#ifdef __cplusplus
}
#endif

#endif /* IPC_MAILBOX_H */
