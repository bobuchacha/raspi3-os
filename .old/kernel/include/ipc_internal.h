/*
 * ipc_internal.h
 *
 * File purpose:
 *   Private object layouts and helper declarations shared across implementation
 *   units.
 *
 * Design notes:
 *   Public callers see opaque pointers. Internal code sees the full object
 *   layout so it can manipulate state, reference counts, and linked lists.
 *
 *   All three object families use a simple singly-linked registry hanging from
 *   the module context. That keeps lookup logic small and predictable for this
 *   first scaffold. A future kernel build can replace these lists with handle
 *   tables or hashed namespaces without changing the public API.
 */
#ifndef IPC_INTERNAL_H
#define IPC_INTERNAL_H

#include "ipc_debug.h"

struct IpcEventStruct {
    /* Stable open-by-name key stored inside the object. */
    char name[IPC_NAME_MAX];
    /* true means the event stays signaled until reset. */
    bool manualReset;
    /* Current signal state consumed by waiters. */
    bool signaled;
    /* Placeholder waiter count for future scheduler-backed blocking waits. */
    uint32_t waiterCount;
    /* Number of references holding this object alive. */
    uint32_t referenceCount;
    /* Monotonic count of successful set operations. */
    uint64_t setCount;

    /* Next event in the context-owned registry list. */
    struct IpcEventStruct *nextEvent;
};

struct IpcMappingStruct {
    /* Stable open-by-name key. */
    char name[IPC_NAME_MAX];
    /* Total byte size of the shared region. */
    size_t size;
    /* Number of active views handed out through ipc_map_view. */
    uint32_t activeViews;
    /* Number of references currently holding the mapping alive. */
    uint32_t referenceCount;
    /* Increments on every successful write into the mapping. */
    uint64_t sequence;
    /* Heap-backed placeholder for the future VM-backed shared region. */
    uint8_t *storage;

    /* Next mapping in the context-owned registry list. */
    struct IpcMappingStruct *nextMapping;
};

struct IpcMailboxStruct {
    /* Stable open-by-name key. */
    char name[IPC_NAME_MAX];
    /* Maximum payload size that fits inside one slot. */
    size_t slotSize;
    /* Number of slots allocated in the ring buffer. */
    size_t slotCount;
    /* Number of messages currently queued. */
    size_t messageCount;
    /* Producer position: next slot to fill. */
    size_t headIndex;
    /* Consumer position: next slot to drain. */
    size_t tailIndex;
    /* Highest observed queue depth, useful for sizing decisions. */
    size_t highWatermark;
    /* Number of references holding the mailbox alive. */
    uint32_t referenceCount;
    /* Flat byte buffer that stores slot payloads. */
    uint8_t *storage;
    /* Per-slot payload lengths so dequeues know how many bytes to copy. */
    size_t *messageLengths;

    /* Next mailbox in the context-owned registry list. */
    struct IpcMailboxStruct *nextMailbox;
};

struct IpcContextStruct {
    /* Host-kernel callbacks captured at module initialization time. */
    IPC_KERNELAPI api;
    /* Cached flag to avoid repeated allocator capability checks. */
    bool hasCustomAlloc;

    /* Head of the event registry list. */
    PIPC_EVENT eventHead;
    /* Head of the mapping registry list. */
    PIPC_MAPPING mappingHead;
    /* Head of the mailbox registry list. */
    PIPC_MAILBOX mailboxHead;
};

/* Allocate module-owned memory using kernel callbacks when provided. */
void *ipc_alloc(PIPC_CONTEXT context, size_t size);
/* Free module-owned memory through the matching allocator path. */
void ipc_free(PIPC_CONTEXT context, void *memory);
/* Copy a caller-provided name into a fixed-size object field safely. */
void ipc_copy_name(char *destination, const char *source);
/* Emit an optional diagnostic line through the host logger. */
void ipc_trace(PIPC_CONTEXT context, int level, const char *message);

/* Linear lookup helpers over the context-owned registries. */
PIPC_EVENT   ipc_lookup_event(PCIPC_CONTEXT context, const char *name);
PIPC_MAPPING ipc_lookup_mapping(PCIPC_CONTEXT context, const char *name);
PIPC_MAILBOX ipc_lookup_mailbox(PCIPC_CONTEXT context, const char *name);

/* Unlink helpers used by close paths once reference counts reach zero. */
void ipc_unlink_event(PIPC_CONTEXT context, PIPC_EVENT event);
void ipc_unlink_mapping(PIPC_CONTEXT context, PIPC_MAPPING mapping);
void ipc_unlink_mailbox(PIPC_CONTEXT context, PIPC_MAILBOX mailbox);

#endif /* IPC_INTERNAL_H */
