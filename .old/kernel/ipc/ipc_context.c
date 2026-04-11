/*
 * ipc_context.c
 *
 * File name:
 *   ipc_context.c
 *
 * Purpose:
 *   Implements module-level lifetime helpers and shared utility routines used
 *   by every IPC object family.
 *
 * Design:
 *   This file centralizes the common mechanics that would otherwise be repeated
 *   across event, mapping, and mailbox implementation units:
 *   - memory allocation and free policy
 *   - safe fixed-size name copying
 *   - named-object lookup in the context registries
 *   - unlink helpers used by final close paths
 *   - full context creation and teardown
 *
 * Procedure call overview:
 *   ipc_init()
 *     -> create a fresh context
 *     -> capture optional kernel callbacks
 *   create/open paths in other files
 *     -> use lookup helpers here to resolve names
 *   close paths in other files
 *     -> use unlink helpers here before releasing storage
 *   ipc_shutdown()
 *     -> walk all registries and free remaining objects
 */
#include "../include/ipc_internal.h"

#include <string.h>

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
#include <stdlib.h>
#define IPC_HOSTED_BUILD 1
#else
#define IPC_HOSTED_BUILD 0
#endif

void*
ipc_alloc(PIPC_CONTEXT context, size_t size) {
    void* memory;

    /*
     * Allocation policy:
     * - reject impossible requests early
     * - prefer the embedding kernel allocator when available
     * - otherwise fall back to calloc so fresh objects start zeroed
     */
    if (!context || !size) {
        return NULL;
    }

    if (context->hasCustomAlloc && context->api.heapAlloc) {
        memory = context->api.heapAlloc(size);
        if (memory) {
            memset(memory, 0, size);
        }
        return memory;
    }

#if IPC_HOSTED_BUILD
    return calloc(1u, size);
#else
    return NULL;
#endif
}

void ipc_free(PIPC_CONTEXT context, void* memory) {
    /*
     * Free must mirror the allocation path. Mixing allocators would corrupt the
     * host heap, so the custom callback pair is always used together.
     */
    if (!memory) {
        return;
    }

    if (context && context->hasCustomAlloc && context->api.heapFree) {
        context->api.heapFree(memory);
        return;
    }

#if IPC_HOSTED_BUILD
    free(memory);
#else
    (void)context;
#endif
}

void ipc_copy_name(char* destination, const char* source) {
    size_t index;

    /*
     * Names live inside fixed-size object fields.
     *
     * This helper guarantees three invariants:
     * - NULL source becomes an empty string
     * - long source names are truncated safely
     * - destination is always NUL-terminated
     */
    if (!destination) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    for (index = 0u; index + 1u < IPC_NAME_MAX && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

void ipc_trace(PIPC_CONTEXT context, int level, const char* message) {
    /* Logging is optional. The module works even when no logger is provided. */
    if (context && context->api.logLine && message) {
        context->api.logLine(level, message);
    }
}

PIPC_EVENT
ipc_lookup_event(PCIPC_CONTEXT context, const char* name) {
    PIPC_EVENT current;

    /*
     * Lookup algorithm:
     * - validate the search key
     * - linearly walk the event registry
     * - return the first exact name match
     *
     * A linear list is acceptable in this scaffold because object counts stay
     * small during bring-up. A future kernel implementation can switch to a
     * hash table or handle namespace without changing the public API.
     */
    if (!context || !name || !name[0]) {
        return NULL;
    }

    for (current = context->eventHead; current; current = current->nextEvent) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
    }
    return NULL;
}

PIPC_MAPPING
ipc_lookup_mapping(PCIPC_CONTEXT context, const char* name) {
    PIPC_MAPPING current;

    /* Mapping lookup uses the same linear registry walk. */
    if (!context || !name || !name[0]) {
        return NULL;
    }

    for (current = context->mappingHead; current; current = current->nextMapping) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
    }
    return NULL;
}

PIPC_MAILBOX
ipc_lookup_mailbox(PCIPC_CONTEXT context, const char* name) {
    PIPC_MAILBOX current;

    /* Mailbox lookup also walks the context-owned registry. */
    if (!context || !name || !name[0]) {
        return NULL;
    }

    for (current = context->mailboxHead; current; current = current->nextMailbox) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
    }
    return NULL;
}

void ipc_unlink_event(PIPC_CONTEXT context, PIPC_EVENT event) {
    PIPC_EVENT* link;

    /*
     * Unlink by pointer identity.
     *
     * The caller already owns the exact event pointer, so pointer matching is
     * cheaper and less error-prone than re-searching by name.
     */
    if (!context || !event) {
        return;
    }

    for (link = &context->eventHead; *link; link = &(*link)->nextEvent) {
        if (*link == event) {
            *link = event->nextEvent;
            event->nextEvent = NULL;
            return;
        }
    }
}

void ipc_unlink_mapping(PIPC_CONTEXT context, PIPC_MAPPING mapping) {
    PIPC_MAPPING* link;

    /* Same unlink approach used for mappings. */
    if (!context || !mapping) {
        return;
    }

    for (link = &context->mappingHead; *link; link = &(*link)->nextMapping) {
        if (*link == mapping) {
            *link = mapping->nextMapping;
            mapping->nextMapping = NULL;
            return;
        }
    }
}

void ipc_unlink_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox) {
    PIPC_MAILBOX* link;

    /* Same unlink approach used for mailboxes. */
    if (!context || !mailbox) {
        return;
    }

    for (link = &context->mailboxHead; *link; link = &(*link)->nextMailbox) {
        if (*link == mailbox) {
            *link = mailbox->nextMailbox;
            mailbox->nextMailbox = NULL;
            return;
        }
    }
}

IPC_RESULT
ipc_init(PCIPC_KERNELAPI kernelApi, PIPC_CONTEXT* outContext) {
    PIPC_CONTEXT context;

    /*
     * Initialization procedure:
     * - allocate a zeroed context so all registries start empty
     * - copy the kernel callback table by value for stable ownership
     * - cache whether custom allocation is available
     */
    if (!outContext) {
        return IPC_E_INVALID_ARG;
    }

    if (kernelApi && kernelApi->heapAlloc && kernelApi->heapFree) {
        context = (PIPC_CONTEXT)kernelApi->heapAlloc(sizeof(*context));
        if (context) {
            memset(context, 0, sizeof(*context));
        }
    }
#if IPC_HOSTED_BUILD
    else {
        context = (PIPC_CONTEXT)calloc(1u, sizeof(*context));
    }
#else
    else {
        context = NULL;
    }
#endif

    if (!context) {
        return IPC_E_OOM;
    }

    if (kernelApi) {
        context->api = *kernelApi;
        context->hasCustomAlloc = (kernelApi->heapAlloc != NULL && kernelApi->heapFree != NULL);
    }

    *outContext = context;
    return IPC_OK;
}

IPC_RESULT
ipc_shutdown(PIPC_CONTEXT context) {
    PIPC_EVENT   event;
    PIPC_MAPPING mapping;
    PIPC_MAILBOX mailbox;

    /*
     * Shutdown walks each registry and frees remaining objects in-place.
     *
     * This is intentionally strict and simple: once shutdown starts, the entire
     * module context is assumed to be leaving service.
     */
    if (!context) {
        return IPC_E_INVALID_ARG;
    }

    while (context->eventHead) {
        event = context->eventHead;
        context->eventHead = event->nextEvent;
        ipc_free(context, event);
    }

    while (context->mappingHead) {
        mapping = context->mappingHead;
        context->mappingHead = mapping->nextMapping;
        ipc_free(context, mapping->storage);
        ipc_free(context, mapping);
    }

    while (context->mailboxHead) {
        mailbox = context->mailboxHead;
        context->mailboxHead = mailbox->nextMailbox;
        ipc_free(context, mailbox->messageLengths);
        ipc_free(context, mailbox->storage);
        ipc_free(context, mailbox);
    }

    ipc_free(context, context);
    return IPC_OK;
}
