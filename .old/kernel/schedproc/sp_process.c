/*
 * sp_process.c
 *
 * Process and thread lifetime helpers for the schedproc scaffold.
 * This file owns only object allocation, list linking, and state transitions.
 * Queue placement and dispatch decisions stay in sp_scheduler.c.
 */
#include "../include/sp_internal.h"

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
#include <stdlib.h>
#include <string.h>
#else
#include "../include/memory.h"
#include "../include/utils.h"
#endif

 /*
  * Allocate zeroed memory before a context object exists.
  *
  * sp_init cannot call sp_alloc yet because the allocator callback table lives
  * inside the context being constructed here.
  */
static void* sp_alloc_bootstrap(PCSP_KERNELAPI api, size_t size) {
    void* memory;

    if (api && api->heapAlloc) {
        memory = api->heapAlloc(size);
    }
    else {
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
        memory = calloc(1u, size);
#else
        memory = (void*)kmalloc((int)(size ? size : 1u));
#endif
    }

    if (!memory) {
        return NULL;
    }

#if !defined(__STDC_HOSTED__) || (__STDC_HOSTED__ != 1)
    memzero((Address)memory, (int)size);
#endif
    return memory;
}

/*
 * Allocate zeroed heap storage through the active schedproc allocator.
 */
void* sp_alloc(PSP_CONTEXT context, size_t size) {
    if (context && context->api.heapAlloc) {
        return context->api.heapAlloc(size);
    }

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
    return calloc(1u, size);
#else
    void* memory;

    if (!size) {
        size = 1u;
    }

    memory = (void*)kmalloc((int)size);
    if (memory) {
        memzero((Address)memory, (int)size);
    }
    return memory;
#endif
}

/*
 * Release heap storage through the same allocator family that created it.
 */
void sp_free(PSP_CONTEXT context, void* memory) {
    if (!memory) {
        return;
    }
    if (context && context->api.heapFree) {
        context->api.heapFree(memory);
        return;
    }

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
    free(memory);
#else
    kfree((Address)memory);
#endif
}

/*
 * Copy one externally supplied name into a fixed-width schedproc buffer.
 */
void sp_copy_name(char* destination, const char* source) {
    size_t index;

    if (!destination) {
        return;
    }
    if (!source) {
        destination[0] = '\0';
        return;
    }

    for (index = 0; index + 1u < SP_NAME_MAX && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

/*
 * Free every process and thread record still owned by a context during final
 * shutdown. Normal runtime teardown uses the explicit destroy helpers instead.
 */
static void sp_destroy_process_list(PSP_CONTEXT context) {
    PSP_PROCESS process;

    if (!context) {
        return;
    }

    process = context->processHead;
    while (process) {
        PSP_PROCESS next_process = process->nextProcess;
        PSP_THREAD thread = process->threadHead;

        while (thread) {
            PSP_THREAD next_thread = thread->nextInProcess;
            sp_free(context, thread);
            thread = next_thread;
        }

        sp_free(context, process);
        process = next_process;
    }

    context->processHead = NULL;
}

/*
 * Create the root schedproc context and seed the first pid/tid values.
 */
SP_RESULT sp_init(PCSP_KERNELAPI api, PSP_CONTEXT* outContext) {
    PSP_CONTEXT context;

    if (!outContext) {
        return SP_E_INVALID_ARG;
    }

    context = (PSP_CONTEXT)sp_alloc_bootstrap(api, sizeof(*context));
    if (!context) {
        return SP_E_OOM;
    }

    if (api) {
        context->api = *api;
        context->hasCustomAlloc = (api->heapAlloc != NULL) || (api->heapFree != NULL);
    }
    context->nextPid = 1u;
    context->nextTid = 1u;
    *outContext = context;
    return SP_OK;
}

/*
 * Destroy the schedproc context and every remaining object it still owns.
 */
SP_RESULT sp_shutdown(PSP_CONTEXT context) {
    if (!context) {
        return SP_E_INVALID_ARG;
    }

    sp_destroy_process_list(context);

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
    free(context);
#else
    if (context->api.heapFree) {
        context->api.heapFree(context);
    }
    else {
        kfree((Address)context);
    }
#endif
    return SP_OK;
}

/*
 * Make a new process record and place it in the global process list.
 *
 * Kid version: this is the moment we write a new name into the class roster,
 * but the process has not started running any threads yet.
 */
SP_RESULT sp_create_process(PSP_CONTEXT context, const char* name, PSP_PROCESS* outProcess) {
    PSP_PROCESS process;

    if (!context || !outProcess) {
        return SP_E_INVALID_ARG;
    }

    process = (PSP_PROCESS)sp_alloc(context, sizeof(*process));
    if (!process) {
        return SP_E_OOM;
    }

    memset(process, 0, sizeof(*process));
    process->processId = context->nextPid++;
    process->state = SP_PROCESS_STARTING;
    sp_copy_name(process->name, name);

    process->nextProcess = context->processHead;
    context->processHead = process;
    *outProcess = process;
    return SP_OK;
}

/*
 * Transition a process into EXITING and mark all remaining live threads as
 * dying so later destroy paths can reclaim them deterministically.
 */
SP_RESULT sp_begin_process_exit(PSP_CONTEXT context, PSP_PROCESS process) {
    PSP_THREAD thread;

    (void)context;
    if (!process) {
        return SP_E_INVALID_ARG;
    }
    if (process->state == SP_PROCESS_DEAD) {
        return SP_E_STATE;
    }

    process->state = SP_PROCESS_EXITING;
    for (thread = process->threadHead; thread; thread = thread->nextInProcess) {
        if (thread->state != SP_THREAD_DEAD) {
            thread->state = SP_THREAD_DYING;
        }
    }
    return SP_OK;
}

/*
 * Unlink and free one process record after all owned threads have been removed.
 */
SP_RESULT sp_destroy_process(PSP_CONTEXT context, PSP_PROCESS process) {
    PSP_PROCESS* link;

    if (!context || !process) {
        return SP_E_INVALID_ARG;
    }
    if (process->threadCount != 0u || process->threadHead != NULL) {
        return SP_E_STATE;
    }

    link = &context->processHead;
    while (*link && *link != process) {
        link = &(*link)->nextProcess;
    }
    if (*link != process) {
        return SP_E_STATE;
    }

    *link = process->nextProcess;
    process->nextProcess = NULL;
    process->state = SP_PROCESS_DEAD;
    sp_free(context, process);
    return SP_OK;
}

/*
 * Copy one process snapshot into caller-owned storage for diagnostics.
 */
SP_RESULT sp_query_process(PSP_PROCESS process, SP_PROCESSINFO* outInfo) {
    if (!process || !outInfo) {
        return SP_E_INVALID_ARG;
    }

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->processId = process->processId;
    outInfo->state = process->state;
    outInfo->threadCount = process->threadCount;
    sp_copy_name(outInfo->name, process->name);
    return SP_OK;
}

/*
 * Make one thread object and attach it to its parent process.
 *
 * New threads start suspended on purpose. That gives the outer kernel code one
 * quiet setup step before the scheduler is allowed to run the thread.
 */
SP_RESULT sp_create_thread(
    PSP_CONTEXT context,
    PSP_PROCESS process,
    const char* name,
    uint8_t     basePriority,
    uint32_t    quantum,
    PSP_THREAD* outThread) {
    PSP_THREAD thread;

    if (!context || !process || !outThread || basePriority > SP_MAX_PRIORITY || !quantum) {
        return SP_E_INVALID_ARG;
    }
    if (process->state != SP_PROCESS_STARTING && process->state != SP_PROCESS_NORMAL) {
        return SP_E_STATE;
    }

    thread = (PSP_THREAD)sp_alloc(context, sizeof(*thread));
    if (!thread) {
        return SP_E_OOM;
    }

    memset(thread, 0, sizeof(*thread));
    thread->threadId = context->nextTid++;
    thread->owner = process;
    thread->basePriority = basePriority;
    thread->currentPriority = basePriority;
    thread->quantum = quantum;
    thread->quantumLeft = quantum;
    thread->suspendCount = 1u;
    thread->state = SP_THREAD_SUSPENDED;
    sp_copy_name(thread->name, name);

    thread->nextInProcess = process->threadHead;
    process->threadHead = thread;
    process->threadCount += 1u;
    if (!process->mainThread) {
        /* The first thread becomes the process anchor and marks it as alive. */
        process->mainThread = thread;
        process->state = SP_PROCESS_NORMAL;
    }

    *outThread = thread;
    return SP_OK;
}

/*
 * Remove one "do not run yet" vote from the thread.
 *
 * When the vote count reaches zero, the thread is allowed to move from the
 * parked state into the normal created state.
 */
SP_RESULT sp_resume_thread(PSP_CONTEXT context, PSP_THREAD thread) {
    (void)context;
    if (!thread) {
        return SP_E_INVALID_ARG;
    }
    if (!thread->suspendCount) {
        return SP_E_STATE;
    }

    thread->suspendCount -= 1u;
    if (!thread->suspendCount && thread->state == SP_THREAD_SUSPENDED) {
        thread->state = SP_THREAD_CREATED;
    }
    return SP_OK;
}

/*
 * Raise the suspend depth and move the thread back into SUSPENDED state.
 */
SP_RESULT sp_suspend_thread(PSP_CONTEXT context, PSP_THREAD thread) {
    (void)context;
    if (!thread) {
        return SP_E_INVALID_ARG;
    }
    if (thread->state == SP_THREAD_DEAD || thread->state == SP_THREAD_DYING) {
        return SP_E_STATE;
    }

    thread->suspendCount += 1u;
    thread->state = SP_THREAD_SUSPENDED;
    return SP_OK;
}

/*
 * Mark one thread dead after first removing it from any scheduler-owned list.
 */
SP_RESULT sp_mark_thread_dead(PSP_CONTEXT context, PSP_THREAD thread) {
    if (!context || !thread) {
        return SP_E_INVALID_ARG;
    }

    sp_forget_thread_from_scheduler(context, thread);
    thread->wakeTick = 0u;
    thread->blockedSince = context->nowTick;
    thread->state = SP_THREAD_DEAD;
    return SP_OK;
}

/*
 * Destroy one dead thread and update its owner's thread list and main-thread
 * pointer on the way out.
 */
SP_RESULT sp_destroy_thread(PSP_CONTEXT context, PSP_THREAD thread) {
    PSP_THREAD* link;
    PSP_PROCESS owner;

    if (!context || !thread) {
        return SP_E_INVALID_ARG;
    }

    owner = thread->owner;
    if (!owner) {
        return SP_E_STATE;
    }
    if (thread->state != SP_THREAD_DEAD && thread->state != SP_THREAD_DYING) {
        return SP_E_STATE;
    }

    sp_forget_thread_from_scheduler(context, thread);

    link = &owner->threadHead;
    while (*link && *link != thread) {
        link = &(*link)->nextInProcess;
    }
    if (*link != thread) {
        return SP_E_STATE;
    }

    *link = thread->nextInProcess;
    thread->nextInProcess = NULL;

    if (owner->mainThread == thread) {
        owner->mainThread = owner->threadHead;
    }
    if (owner->threadCount > 0u) {
        owner->threadCount -= 1u;
    }
    if (owner->threadCount == 0u && owner->state == SP_PROCESS_EXITING) {
        owner->state = SP_PROCESS_DEAD;
    }

    sp_free(context, thread);
    return SP_OK;
}

/*
 * Copy one thread snapshot into caller-owned storage for diagnostics.
 */
SP_RESULT sp_query_thread(PSP_THREAD thread, SP_THREADINFO* outInfo) {
    if (!thread || !outInfo) {
        return SP_E_INVALID_ARG;
    }

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->threadId = thread->threadId;
    outInfo->basePriority = thread->basePriority;
    outInfo->currentPriority = thread->currentPriority;
    outInfo->quantum = thread->quantum;
    outInfo->quantumLeft = thread->quantumLeft;
    outInfo->wakeTick = thread->wakeTick;
    outInfo->state = thread->state;
    outInfo->runnableSince = thread->runnableSince;
    outInfo->blockedSince = thread->blockedSince;
    sp_copy_name(outInfo->name, thread->name);
    return SP_OK;
}

/*
 * Return a short printable name for one process state enum value.
 */
const char* sp_process_state_name(SP_PROCESS_STATE state) {
    switch (state) {
    case SP_PROCESS_STARTING:
        return "STARTING";
    case SP_PROCESS_NORMAL:
        return "NORMAL";
    case SP_PROCESS_EXITING:
        return "EXITING";
    case SP_PROCESS_DEAD:
        return "DEAD";
    default:
        return "UNKNOWN";
    }
}

/*
 * Return a short printable name for one thread state enum value.
 */
const char* sp_thread_state_name(SP_THREAD_STATE state) {
    switch (state) {
    case SP_THREAD_CREATED:
        return "CREATED";
    case SP_THREAD_SUSPENDED:
        return "SUSPENDED";
    case SP_THREAD_RUNNABLE:
        return "RUNNABLE";
    case SP_THREAD_RUNNING:
        return "RUNNING";
    case SP_THREAD_BLOCKED:
        return "BLOCKED";
    case SP_THREAD_SLEEPING:
        return "SLEEPING";
    case SP_THREAD_DYING:
        return "DYING";
    case SP_THREAD_DEAD:
        return "DEAD";
    default:
        return "UNKNOWN";
    }
}
