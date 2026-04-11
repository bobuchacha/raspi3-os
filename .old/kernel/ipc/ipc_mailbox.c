/*
 * ipc_mailbox.c
 *
 * File name:
 *   ipc_mailbox.c
 *
 * Purpose:
 *   Implements named mailbox queues for message-oriented IPC.
 *
 * Design:
 *   The mailbox uses a bounded ring buffer with fixed-size slots. This keeps
 *   memory usage deterministic, which is a strong fit for kernel bring-up and
 *   embedded-style IPC systems.
 */
#include "../include/ipc_internal.h"

#include <string.h>

IPC_RESULT
ipc_create_mailbox(PIPC_CONTEXT context, const char *name, size_t slotSize, size_t slotCount, PIPC_MAILBOX *outMailbox) {
    PIPC_MAILBOX mailbox;

    /*
     * Create procedure:
     * - validate queue geometry and output parameters
     * - reject duplicate names
     * - allocate the mailbox record, payload storage, and length table
     * - link the mailbox into the registry
     */

    if (!context || !name || !name[0] || !slotSize || !slotCount || !outMailbox) {
        return IPC_E_INVALID_ARG;
    }
    if (ipc_lookup_mailbox(context, name)) {
        return IPC_E_EXISTS;
    }

    mailbox = (PIPC_MAILBOX)ipc_alloc(context, sizeof(*mailbox));
    if (!mailbox) {
        return IPC_E_OOM;
    }

    mailbox->storage = (uint8_t *)ipc_alloc(context, slotSize * slotCount);
    mailbox->messageLengths = (size_t *)ipc_alloc(context, sizeof(size_t) * slotCount);
    if (!mailbox->storage || !mailbox->messageLengths) {
        ipc_free(context, mailbox->messageLengths);
        ipc_free(context, mailbox->storage);
        ipc_free(context, mailbox);
        return IPC_E_OOM;
    }

    ipc_copy_name(mailbox->name, name);
    mailbox->slotSize = slotSize;
    mailbox->slotCount = slotCount;
    mailbox->referenceCount = 1u;
    mailbox->nextMailbox = context->mailboxHead;
    context->mailboxHead = mailbox;

    *outMailbox = mailbox;
    return IPC_OK;
}

IPC_RESULT
ipc_open_mailbox(PIPC_CONTEXT context, const char *name, PIPC_MAILBOX *outMailbox) {
    PIPC_MAILBOX mailbox;

    /* Open is lookup plus reference acquisition. */

    if (!context || !name || !name[0] || !outMailbox) {
        return IPC_E_INVALID_ARG;
    }

    mailbox = ipc_lookup_mailbox(context, name);
    if (!mailbox) {
        return IPC_E_NOT_FOUND;
    }

    mailbox->referenceCount += 1u;
    *outMailbox = mailbox;
    return IPC_OK;
}

IPC_RESULT
ipc_close_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox) {
    /* Release one reference and destroy the mailbox on the final close. */
    if (!context || !mailbox || !mailbox->referenceCount) {
        return IPC_E_INVALID_ARG;
    }

    mailbox->referenceCount -= 1u;
    if (mailbox->referenceCount == 0u) {
        ipc_unlink_mailbox(context, mailbox);
        ipc_free(context, mailbox->messageLengths);
        ipc_free(context, mailbox->storage);
        ipc_free(context, mailbox);
    }
    return IPC_OK;
}

IPC_RESULT
ipc_send_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox, const void *message, size_t length) {
    size_t slotOffset;

    (void)context;

    if (!mailbox || !message || !length) {
        return IPC_E_INVALID_ARG;
    }
    if (length > mailbox->slotSize) {
        return IPC_E_BUFFER_TOO_SMALL;
    }
    if (mailbox->messageCount == mailbox->slotCount) {
        return IPC_E_FULL;
    }

    /*
     * Enqueue algorithm:
     * - headIndex marks the next free slot for the producer
     * - tailIndex marks the oldest queued message for the consumer
     * - messageCount distinguishes full from empty, so every slot is usable
     * - after copying, headIndex advances modulo slotCount
     */
    slotOffset = mailbox->headIndex * mailbox->slotSize;
    memcpy(mailbox->storage + slotOffset, message, length);
    mailbox->messageLengths[mailbox->headIndex] = length;
    mailbox->headIndex = (mailbox->headIndex + 1u) % mailbox->slotCount;
    mailbox->messageCount += 1u;

    if (mailbox->messageCount > mailbox->highWatermark) {
        mailbox->highWatermark = mailbox->messageCount;
    }
    return IPC_OK;
}

IPC_RESULT
ipc_receive_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox, void *buffer, size_t bufferSize, size_t *outLength) {
    size_t messageLength;
    size_t slotOffset;

    (void)context;

    /*
     * Dequeue algorithm:
     * - inspect the message length at tailIndex
     * - reject too-small destination buffers before modifying queue state
     * - copy the payload out
     * - clear slot metadata and advance tailIndex modulo slotCount
     */
    if (!mailbox || !buffer || !outLength) {
        return IPC_E_INVALID_ARG;
    }
    if (mailbox->messageCount == 0u) {
        return IPC_E_EMPTY;
    }

    messageLength = mailbox->messageLengths[mailbox->tailIndex];
    *outLength = messageLength;
    if (bufferSize < messageLength) {
        return IPC_E_BUFFER_TOO_SMALL;
    }

    slotOffset = mailbox->tailIndex * mailbox->slotSize;
    memcpy(buffer, mailbox->storage + slotOffset, messageLength);
    mailbox->messageLengths[mailbox->tailIndex] = 0u;
    mailbox->tailIndex = (mailbox->tailIndex + 1u) % mailbox->slotCount;
    mailbox->messageCount -= 1u;
    return IPC_OK;
}

IPC_RESULT
ipc_query_mailbox(PIPC_MAILBOX mailbox, IPC_MAILBOXINFO *outInfo) {
    /* Snapshot the current mailbox state into caller-owned storage. */
    if (!mailbox || !outInfo) {
        return IPC_E_INVALID_ARG;
    }

    ipc_copy_name(outInfo->name, mailbox->name);
    outInfo->slotSize = mailbox->slotSize;
    outInfo->slotCount = mailbox->slotCount;
    outInfo->messageCount = mailbox->messageCount;
    outInfo->highWatermark = mailbox->highWatermark;
    outInfo->referenceCount = mailbox->referenceCount;
    return IPC_OK;
}
