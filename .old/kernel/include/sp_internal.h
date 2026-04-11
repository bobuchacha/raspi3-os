/*
 * sp_internal.h
 *
 * Internal data structures shared by scheduler/process-manager implementation units.
 */
#ifndef SP_INTERNAL_H
#define SP_INTERNAL_H

#include "sp_wait.h"

struct SpThreadStruct {
    uint32_t threadId;
    char     name[SP_NAME_MAX];

    uint8_t basePriority;
    uint8_t currentPriority;
    uint8_t suspendCount;
    uint8_t reserved0;

    uint32_t quantum;
    uint32_t quantumLeft;
    uint64_t wakeTick;

    SP_THREAD_STATE         state;
    struct SpProcessStruct *owner;

    uint64_t userTicks;
    uint64_t kernelTicks;
    uint64_t runnableSince;
    uint64_t blockedSince;

    struct SpThreadStruct *rqPrev;
    struct SpThreadStruct *rqNext;
    struct SpThreadStruct *sleepPrev;
    struct SpThreadStruct *sleepNext;
    struct SpThreadStruct *nextInProcess;
};

struct SpProcessStruct {
    uint32_t         processId;
    char             name[SP_NAME_MAX];
    SP_PROCESS_STATE state;

    uint32_t                threadCount;
    struct SpThreadStruct  *threadHead;
    struct SpThreadStruct  *mainThread;
    struct SpProcessStruct *nextProcess;
};

typedef struct SpReadyQueueStruct {
    PSP_THREAD head;
    PSP_THREAD tail;
} SP_READYQUEUE;

struct SpContextStruct {
    SP_KERNELAPI api;
    bool         hasCustomAlloc;
    uint32_t     nextPid;
    uint32_t     nextTid;

    PSP_PROCESS processHead;

    SP_READYQUEUE ready[SP_READY_LEVELS];
    uint32_t      readyBitmap;
    PSP_THREAD    sleepHead;
    PSP_THREAD    current;
    uint64_t      nowTick;
    uint64_t      nextReschedTick;
};

void *sp_alloc(PSP_CONTEXT context, size_t size);
void  sp_free(PSP_CONTEXT context, void *memory);
void  sp_copy_name(char *destination, const char *source);
void  sp_refresh_deadline(PSP_CONTEXT context);
void  sp_forget_thread_from_scheduler(PSP_CONTEXT context, PSP_THREAD thread);

#endif /* SP_INTERNAL_H */
