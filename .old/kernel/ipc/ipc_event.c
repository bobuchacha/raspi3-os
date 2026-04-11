/*
 * ipc_event.c
 *
 * File name:
 *   ipc_event.c
 *
 * Purpose:
 *   Implements named event objects for notification-style IPC.
 *
 * Design:
 *   The event model follows the familiar Windows CE / Win32 semantics:
 *   - callers create or open events by name
 *   - events are either manual-reset or auto-reset
 *   - event lifetime is controlled by reference counting
 *
 * Current logic boundary:
 *   This scaffold implements event state transitions, not full scheduler-backed
 *   blocking waits. A wait succeeds immediately if the event is signaled and
 *   otherwise returns a timeout-style result. Real wait-queue integration will
 *   be added when this module is wired to my-schedproc.
 */
#include "../include/ipc_internal.h"

IPC_RESULT
ipc_create_event(PIPC_CONTEXT context, const char *name, bool manualReset, bool initialState, PIPC_EVENT *outEvent) {
    PIPC_EVENT event;

    /*
     * Create procedure:
     * 1. validate caller input
     * 2. reject duplicate names in the same context namespace
     * 3. allocate a fresh event object
     * 4. initialize its state and reference count
     * 5. link it into the event registry and return it
     */

    if (!context || !name || !name[0] || !outEvent) {
        return IPC_E_INVALID_ARG;
    }
    if (ipc_lookup_event(context, name)) {
        return IPC_E_EXISTS;
    }

    event = (PIPC_EVENT)ipc_alloc(context, sizeof(*event));
    if (!event) {
        return IPC_E_OOM;
    }

    ipc_copy_name(event->name, name);
    event->manualReset = manualReset;
    event->signaled = initialState;
    event->referenceCount = 1u;
    event->nextEvent = context->eventHead;
    context->eventHead = event;

    *outEvent = event;
    return IPC_OK;
}

IPC_RESULT
ipc_open_event(PIPC_CONTEXT context, const char *name, PIPC_EVENT *outEvent) {
    PIPC_EVENT event;

    /* Open is a lookup followed by reference acquisition. */

    if (!context || !name || !name[0] || !outEvent) {
        return IPC_E_INVALID_ARG;
    }

    event = ipc_lookup_event(context, name);
    if (!event) {
        return IPC_E_NOT_FOUND;
    }

    event->referenceCount += 1u;
    *outEvent = event;
    return IPC_OK;
}

IPC_RESULT
ipc_close_event(PIPC_CONTEXT context, PIPC_EVENT event) {
    /*
     * Close procedure:
     * - drop one reference
     * - unlink the event from the registry only on the final close
     * - free the event record after it is no longer reachable
     */
    if (!context || !event || !event->referenceCount) {
        return IPC_E_INVALID_ARG;
    }

    event->referenceCount -= 1u;
    if (event->referenceCount == 0u) {
        ipc_unlink_event(context, event);
        ipc_free(context, event);
    }
    return IPC_OK;
}

IPC_RESULT
ipc_set_event(PIPC_CONTEXT context, PIPC_EVENT event) {
    (void)context;

    /*
     * Set transitions the object into the signaled state.
     *
     * In the current scaffold this is only a state update plus accounting.
     * In the future it will also wake blocked waiters.
     */

    if (!event) {
        return IPC_E_INVALID_ARG;
    }

    event->signaled = true;
    event->setCount += 1u;
    return IPC_OK;
}

IPC_RESULT
ipc_reset_event(PIPC_CONTEXT context, PIPC_EVENT event) {
    (void)context;

    /* Reset simply clears the signaled state. */

    if (!event) {
        return IPC_E_INVALID_ARG;
    }

    event->signaled = false;
    return IPC_OK;
}

IPC_RESULT
ipc_wait_event(PIPC_CONTEXT context, PIPC_EVENT event, uint64_t timeoutTicks) {
    (void)context;
    (void)timeoutTicks;

    if (!event) {
        return IPC_E_INVALID_ARG;
    }

    /*
     * Wait algorithm in the scaffold:
     * - if already signaled, succeed immediately
     * - if auto-reset, consume the signal on that success path
     * - if not signaled, return IPC_E_TIMEOUT because true blocking is not
     *   connected yet
     *
     * Future procedure:
     * - construct a wait node for the current thread
     * - enqueue it on the event wait list
     * - block through the scheduler
     * - wake one waiter for auto-reset or all waiters for manual-reset
     */
    if (event->signaled) {
        if (!event->manualReset) {
            event->signaled = false;
        }
        return IPC_OK;
    }

    event->waiterCount += 1u;
    event->waiterCount -= 1u;
    return IPC_E_TIMEOUT;
}

IPC_RESULT
ipc_query_event(PIPC_EVENT event, IPC_EVENTINFO *outInfo) {
    /* Copy the current event state into caller-owned storage. */
    if (!event || !outInfo) {
        return IPC_E_INVALID_ARG;
    }

    ipc_copy_name(outInfo->name, event->name);
    outInfo->manualReset = event->manualReset;
    outInfo->signaled = event->signaled;
    outInfo->waiterCount = event->waiterCount;
    outInfo->referenceCount = event->referenceCount;
    outInfo->setCount = event->setCount;
    return IPC_OK;
}
