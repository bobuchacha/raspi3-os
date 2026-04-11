/*
 * sp_thread.h
 *
 * Public thread-management API.
 */
#ifndef SP_THREAD_H
#define SP_THREAD_H

#include "sp_process.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpThreadInfoStruct {
    uint32_t        threadId;
    char            name[SP_NAME_MAX];
    uint8_t         basePriority;
    uint8_t         currentPriority;
    uint32_t        quantum;
    uint32_t        quantumLeft;
    uint64_t        wakeTick;
    SP_THREAD_STATE state;
    uint64_t        runnableSince;
    uint64_t        blockedSince;
} SP_THREADINFO;

/* Create one thread object in suspended state. */
SP_RESULT sp_create_thread(
    PSP_CONTEXT context,
    PSP_PROCESS process,
    const char *name,
    uint8_t     basePriority,
    uint32_t    quantum,
    PSP_THREAD *outThread);

/* Release one suspended thread so it can be scheduled. */
SP_RESULT sp_resume_thread(PSP_CONTEXT context, PSP_THREAD thread);

/* Increase suspend depth and stop the thread from being runnable. */
SP_RESULT sp_suspend_thread(PSP_CONTEXT context, PSP_THREAD thread);

/* Mark one thread dead and detach it from scheduler queues. */
SP_RESULT sp_mark_thread_dead(PSP_CONTEXT context, PSP_THREAD thread);

/* Destroy one dead thread and unlink it from its owner process. */
SP_RESULT sp_destroy_thread(PSP_CONTEXT context, PSP_THREAD thread);

/* Read one thread snapshot into caller-owned memory. */
SP_RESULT sp_query_thread(PSP_THREAD thread, SP_THREADINFO *outInfo);

const char *sp_process_state_name(SP_PROCESS_STATE state);
const char *sp_thread_state_name(SP_THREAD_STATE state);

#ifdef __cplusplus
}
#endif

#endif /* SP_THREAD_H */
