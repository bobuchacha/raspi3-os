/*
 * ipc_mapping.c
 *
 * File name:
 *   ipc_mapping.c
 *
 * Purpose:
 *   Implements named shared-memory mappings for fast payload exchange.
 *
 * Design:
 *   The scaffold separates the mapping object contract from the eventual VM
 *   implementation. Today, mappings are backed by heap memory. Later, the same
 *   API can be preserved while the backend changes to real shared pages.
 */
#include "../include/ipc_internal.h"

#include <string.h>

IPC_RESULT
ipc_create_mapping(PIPC_CONTEXT context, const char *name, size_t size, PIPC_MAPPING *outMapping) {
    PIPC_MAPPING mapping;

    /*
     * Create procedure:
     * - validate arguments and reject zero-sized mappings
     * - reject duplicate names
     * - allocate the object record and backing storage
     * - link the new mapping into the context registry
     */

    if (!context || !name || !name[0] || !size || !outMapping) {
        return IPC_E_INVALID_ARG;
    }
    if (ipc_lookup_mapping(context, name)) {
        return IPC_E_EXISTS;
    }

    mapping = (PIPC_MAPPING)ipc_alloc(context, sizeof(*mapping));
    if (!mapping) {
        return IPC_E_OOM;
    }

    mapping->storage = (uint8_t *)ipc_alloc(context, size);
    if (!mapping->storage) {
        ipc_free(context, mapping);
        return IPC_E_OOM;
    }

    ipc_copy_name(mapping->name, name);
    mapping->size = size;
    mapping->referenceCount = 1u;
    mapping->nextMapping = context->mappingHead;
    context->mappingHead = mapping;

    *outMapping = mapping;
    return IPC_OK;
}

IPC_RESULT
ipc_open_mapping(PIPC_CONTEXT context, const char *name, PIPC_MAPPING *outMapping) {
    PIPC_MAPPING mapping;

    /* Open is lookup plus reference acquisition. */

    if (!context || !name || !name[0] || !outMapping) {
        return IPC_E_INVALID_ARG;
    }

    mapping = ipc_lookup_mapping(context, name);
    if (!mapping) {
        return IPC_E_NOT_FOUND;
    }

    mapping->referenceCount += 1u;
    *outMapping = mapping;
    return IPC_OK;
}

IPC_RESULT
ipc_close_mapping(PIPC_CONTEXT context, PIPC_MAPPING mapping) {
    /* Release one mapping reference and destroy it on the final close. */
    if (!context || !mapping || !mapping->referenceCount) {
        return IPC_E_INVALID_ARG;
    }

    mapping->referenceCount -= 1u;
    if (mapping->referenceCount == 0u) {
        ipc_unlink_mapping(context, mapping);
        ipc_free(context, mapping->storage);
        ipc_free(context, mapping);
    }
    return IPC_OK;
}

IPC_RESULT
ipc_map_view(PIPC_MAPPING mapping, void **outView, size_t *outSize) {
    /*
     * View mapping procedure:
     * - validate the object and output pointer
     * - increment the active view count for diagnostics
     * - return the base address of the shared storage
     *
     * A real kernel implementation would map storage into the caller's address
     * space rather than returning the backing buffer directly.
     */
    if (!mapping || !outView) {
        return IPC_E_INVALID_ARG;
    }

    mapping->activeViews += 1u;
    *outView = mapping->storage;
    if (outSize) {
        *outSize = mapping->size;
    }
    return IPC_OK;
}

IPC_RESULT
ipc_unmap_view(PIPC_MAPPING mapping, void *view) {
    /*
     * Unmap validates that the caller returns the exact pointer previously
     * exposed by ipc_map_view. This keeps misuse visible in the early scaffold.
     */
    if (!mapping || view != mapping->storage) {
        return IPC_E_INVALID_ARG;
    }

    if (mapping->activeViews == 0u) {
        return IPC_E_STATE;
    }

    mapping->activeViews -= 1u;
    return IPC_OK;
}

IPC_RESULT
ipc_write_mapping(PIPC_MAPPING mapping, size_t offset, const void *buffer, size_t length) {
    /*
     * Write algorithm:
     * - validate the destination range before touching memory
     * - copy bytes into the shared storage
     * - increment the sequence counter so readers can detect fresh content
     */
    if (!mapping || (!buffer && length)) {
        return IPC_E_INVALID_ARG;
    }
    if (offset > mapping->size || length > mapping->size - offset) {
        return IPC_E_INVALID_ARG;
    }

    memcpy(mapping->storage + offset, buffer, length);
    mapping->sequence += 1u;
    return IPC_OK;
}

IPC_RESULT
ipc_read_mapping(PIPC_MAPPING mapping, size_t offset, void *buffer, size_t length) {
    /* Read is the symmetric range-checked copy-out path. */
    if (!mapping || (!buffer && length)) {
        return IPC_E_INVALID_ARG;
    }
    if (offset > mapping->size || length > mapping->size - offset) {
        return IPC_E_INVALID_ARG;
    }

    memcpy(buffer, mapping->storage + offset, length);
    return IPC_OK;
}

IPC_RESULT
ipc_query_mapping(PIPC_MAPPING mapping, IPC_MAPPINGINFO *outInfo) {
    /* Snapshot the current mapping state into caller-owned storage. */
    if (!mapping || !outInfo) {
        return IPC_E_INVALID_ARG;
    }

    ipc_copy_name(outInfo->name, mapping->name);
    outInfo->size = mapping->size;
    outInfo->activeViews = mapping->activeViews;
    outInfo->referenceCount = mapping->referenceCount;
    outInfo->sequence = mapping->sequence;
    return IPC_OK;
}
