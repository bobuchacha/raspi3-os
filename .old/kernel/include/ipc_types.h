/*
 * ipc_types.h
 *
 * File purpose:
 *   Defines the shared base types used by every public and internal IPC unit.
 *
 * Design notes:
 *   The module mirrors the structure used by my-loader and my-schedproc:
 *   small public headers, opaque public handles, and internal implementation
 *   structures hidden behind forward declarations.
 *
 *   Public callers only need these enums and opaque pointer types to hold
 *   references to events, mappings, mailboxes, and the module context.
 */
#ifndef IPC_TYPES_H
#define IPC_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum object name length stored directly inside IPC objects.
 *
 * The limit is intentionally fixed-size so object records remain simple and
 * deterministic. The copy helper truncates longer names safely.
 */
#define IPC_NAME_MAX 64u

typedef enum IpcResultEnum {
    /* Operation completed successfully. */
    IPC_OK = 0,
    /* One or more required parameters were NULL, empty, or out of range. */
    IPC_E_INVALID_ARG,
    /* The object was valid, but its current lifecycle state rejects the call. */
    IPC_E_STATE,
    /* Memory allocation failed while creating or growing an object. */
    IPC_E_OOM,
    /* A named object lookup failed. */
    IPC_E_NOT_FOUND,
    /* A create call collided with an already-existing named object. */
    IPC_E_EXISTS,
    /* The bounded mailbox cannot accept another message right now. */
    IPC_E_FULL,
    /* The mailbox currently has no queued message to consume. */
    IPC_E_EMPTY,
    /* A wait-style operation could not complete before its timeout condition. */
    IPC_E_TIMEOUT,
    /* The caller-provided destination buffer is too small for the payload. */
    IPC_E_BUFFER_TOO_SMALL,
    /* Placeholder return used by APIs that are planned but not yet wired. */
    IPC_E_NOT_IMPLEMENTED
} IPC_RESULT;

/*
 * Public object handles are intentionally opaque.
 *
 * This keeps ownership and layout inside the module while still allowing
 * callers to pass object references between API calls.
 */
typedef struct IpcContextStruct IPC_CONTEXT;
typedef struct IpcEventStruct   IPC_EVENT;
typedef struct IpcMappingStruct IPC_MAPPING;
typedef struct IpcMailboxStruct IPC_MAILBOX;

/* Mutable handle aliases used by implementation and tests. */
typedef IPC_CONTEXT *PIPC_CONTEXT;
typedef IPC_EVENT   *PIPC_EVENT;
typedef IPC_MAPPING *PIPC_MAPPING;
typedef IPC_MAILBOX *PIPC_MAILBOX;

/* Read-only handle aliases used by diagnostics and const-safe helpers. */
typedef const IPC_CONTEXT *PCIPC_CONTEXT;
typedef const IPC_EVENT   *PCIPC_EVENT;
typedef const IPC_MAPPING *PCIPC_MAPPING;
typedef const IPC_MAILBOX *PCIPC_MAILBOX;

typedef struct IpcEventInfoStruct {
    /* Name used for open-by-name operations. */
    char name[IPC_NAME_MAX];
    /* true for manual-reset, false for auto-reset. */
    bool manualReset;
    /* Current signal state observed by callers. */
    bool signaled;
    /* Number of blocked waiters once scheduler integration is added. */
    uint32_t waiterCount;
    /* Number of handles/references currently pointing at this event. */
    uint32_t referenceCount;
    /* Count of successful set operations for diagnostics and tracing. */
    uint64_t setCount;
} IPC_EVENTINFO;

typedef struct IpcMappingInfoStruct {
    /* Stable name of the mapping object. */
    char name[IPC_NAME_MAX];
    /* Total byte size of the shared region. */
    size_t size;
    /* Number of currently active views returned by ipc_map_view. */
    uint32_t activeViews;
    /* Number of references currently holding the mapping object alive. */
    uint32_t referenceCount;
    /* Monotonic write sequence incremented on every successful write. */
    uint64_t sequence;
} IPC_MAPPINGINFO;

typedef struct IpcMailboxInfoStruct {
    /* Stable name of the mailbox queue. */
    char name[IPC_NAME_MAX];
    /* Maximum payload size that fits in one mailbox slot. */
    size_t slotSize;
    /* Total number of slots allocated for the ring buffer. */
    size_t slotCount;
    /* Number of messages currently queued for consumption. */
    size_t messageCount;
    /* Highest observed queue depth since creation. */
    size_t highWatermark;
    /* Number of references currently holding the mailbox alive. */
    uint32_t referenceCount;
} IPC_MAILBOXINFO;

#ifdef __cplusplus
}
#endif

#endif /* IPC_TYPES_H */
