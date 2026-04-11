/*
 * ipc_event.h
 *
 * File purpose:
 *   Public API for named event objects.
 *
 * Design notes:
 *   Events model the common Windows CE style of synchronization object:
 *   callers create or open by name, then set, reset, or wait on the object.
 *   The current scaffold implements the state machine but not true scheduler
 *   blocking yet.
 */
#ifndef IPC_EVENT_H
#define IPC_EVENT_H

#include "ipc_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a named event object and insert it into the module context list. */
IPC_RESULT ipc_create_event(PIPC_CONTEXT context, const char *name, bool manualReset, bool initialState, PIPC_EVENT *outEvent);
/* Lookup an existing named event and acquire another reference to it. */
IPC_RESULT ipc_open_event(PIPC_CONTEXT context, const char *name, PIPC_EVENT *outEvent);
/* Release one event reference and destroy the object when the count reaches zero. */
IPC_RESULT ipc_close_event(PIPC_CONTEXT context, PIPC_EVENT event);
/* Transition the event into the signaled state. */
IPC_RESULT ipc_set_event(PIPC_CONTEXT context, PIPC_EVENT event);
/* Transition the event into the non-signaled state. */
IPC_RESULT ipc_reset_event(PIPC_CONTEXT context, PIPC_EVENT event);
/* Attempt to observe or wait for the event to become signaled. */
IPC_RESULT ipc_wait_event(PIPC_CONTEXT context, PIPC_EVENT event, uint64_t timeoutTicks);
/* Copy event state into a caller-owned diagnostics structure. */
IPC_RESULT ipc_query_event(PIPC_EVENT event, IPC_EVENTINFO *outInfo);

#ifdef __cplusplus
}
#endif

#endif /* IPC_EVENT_H */
