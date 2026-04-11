/*
 * sp_scheduler.c
 *
 * Single-CPU scheduler core for the schedproc scaffold.
 *
 * Responsibilities owned here:
 * - ready-queue insertion and removal,
 * - ordered sleep-queue maintenance,
 * - current-thread dispatch and preemption checks,
 * - tick accounting for runnable and sleeping threads,
 * - and deadline calculation for the next scheduling event.
 *
 * This file intentionally avoids process or thread allocation. Object lifetime
 * stays in sp_process.c so scheduler policy and object ownership remain split.
 */
#include "../include/sp_internal.h"
#include "log.h"

 /* Return whether one ready queue currently contains any runnable threads. */
static bool
sp_ready_is_empty(const SP_READYQUEUE* queue) {
    return !queue || !queue->head;
}

/*
 * Remove one thread from the middle of a ready queue.
 *
 * This helper is only used by teardown paths that must unlink a thread even
 * when the caller no longer knows whether it sits at the head, tail, or
 * somewhere in between.
 */
static void
sp_ready_remove(SP_READYQUEUE* queue, PSP_THREAD thread) {
    if (!queue || !thread) {
        return;
    }

    if (thread->rqPrev) {
        thread->rqPrev->rqNext = thread->rqNext;
    }
    else if (queue->head == thread) {
        queue->head = thread->rqNext;
    }

    if (thread->rqNext) {
        thread->rqNext->rqPrev = thread->rqPrev;
    }
    else if (queue->tail == thread) {
        queue->tail = thread->rqPrev;
    }

    thread->rqPrev = NULL;
    thread->rqNext = NULL;
}

/*
 * Insert one runnable thread at either the head or tail of a priority queue.
 * Head insertion is used for immediate preemption scenarios, while tail
 * insertion preserves round-robin fairness among equal-priority peers.
 */
static void
sp_ready_push(SP_READYQUEUE* queue, PSP_THREAD thread, bool atTail) {
    thread->rqPrev = NULL;
    thread->rqNext = NULL;

    if (!queue->head) {
        queue->head = queue->tail = thread;
        return;
    }

    if (atTail) {
        thread->rqPrev = queue->tail;
        queue->tail->rqNext = thread;
        queue->tail = thread;
    }
    else {
        thread->rqNext = queue->head;
        queue->head->rqPrev = thread;
        queue->head = thread;
    }
}

/* Pop the next runnable thread from the head of one priority queue. */
static PSP_THREAD
sp_ready_pop_head(SP_READYQUEUE* queue) {
    PSP_THREAD thread;

    if (!queue || !queue->head) {
        return NULL;
    }

    thread = queue->head;
    queue->head = thread->rqNext;
    if (queue->head) {
        queue->head->rqPrev = NULL;
    }
    else {
        queue->tail = NULL;
    }

    thread->rqPrev = NULL;
    thread->rqNext = NULL;
    return thread;
}

/* Scan the ready bitmap from highest to lowest priority and return the first hit. */
static int
sp_find_best_priority(const PSP_CONTEXT context) {
    uint32_t priority;

    if (!context || !context->readyBitmap) {
        return -1;
    }

    for (priority = 0u; priority < SP_READY_LEVELS; ++priority) {
        if (context->readyBitmap & (1u << priority)) {
            return (int)priority;
        }
    }
    return -1;
}

/* Insert one sleeping thread into wake-time order so the head is always next due. */
static void
sp_sleep_insert_sorted(PSP_CONTEXT context, PSP_THREAD thread) {
    PSP_THREAD current;

    thread->sleepPrev = NULL;
    thread->sleepNext = NULL;

    if (!context->sleepHead) {
        context->sleepHead = thread;
        return;
    }

    current = context->sleepHead;
    while (current && current->wakeTick <= thread->wakeTick) {
        current = current->sleepNext;
    }

    if (!current) {
        PSP_THREAD tail = context->sleepHead;
        while (tail->sleepNext) {
            tail = tail->sleepNext;
        }
        tail->sleepNext = thread;
        thread->sleepPrev = tail;
        return;
    }

    thread->sleepNext = current;
    thread->sleepPrev = current->sleepPrev;
    if (current->sleepPrev) {
        current->sleepPrev->sleepNext = thread;
    }
    else {
        context->sleepHead = thread;
    }
    current->sleepPrev = thread;
}

/* Remove one thread from the sleep queue regardless of its current position. */
static void
sp_sleep_remove(PSP_CONTEXT context, PSP_THREAD thread) {
    if (!context || !thread) {
        return;
    }

    if (thread->sleepPrev) {
        thread->sleepPrev->sleepNext = thread->sleepNext;
    }
    else {
        context->sleepHead = thread->sleepNext;
    }
    if (thread->sleepNext) {
        thread->sleepNext->sleepPrev = thread->sleepPrev;
    }
    thread->sleepPrev = NULL;
    thread->sleepNext = NULL;
}

/*
 * Forget every scheduler queue link that still points at `thread`.
 *
 * The process-manager layer uses this before marking a thread dead or freeing
 * it, so no ready list, sleep list, or `current` slot can retain a dangling
 * pointer into reclaimed memory.
 */
void
sp_forget_thread_from_scheduler(PSP_CONTEXT context, PSP_THREAD thread) {
    uint32_t priority;

    if (!context || !thread) {
        return;
    }

    if (context->current == thread) {
        context->current = NULL;
    }

    if (thread->state == SP_THREAD_RUNNABLE && thread->currentPriority <= SP_MAX_PRIORITY) {
        priority = thread->currentPriority;
        sp_ready_remove(&context->ready[priority], thread);
        if (sp_ready_is_empty(&context->ready[priority])) {
            context->readyBitmap &= ~(1u << priority);
        }
    }

    if (thread->state == SP_THREAD_SLEEPING || thread->sleepPrev || thread->sleepNext || context->sleepHead == thread) {
        sp_sleep_remove(context, thread);
    }

    sp_refresh_deadline(context);
}

/*
 * Pick the next moment when the scheduler must wake up and look around again.
 *
 * Kid version:
 * - the running thread may run out of turns soon,
 * - the first sleeping thread may need to wake up soon,
 * - whichever happens first becomes the next scheduler deadline.
 */
void sp_refresh_deadline(PSP_CONTEXT context) {
    uint64_t deadline = UINT64_MAX;

    if (!context) {
        return;
    }

    if (context->current && context->current->state == SP_THREAD_RUNNING && context->current->quantumLeft) {
        deadline = context->nowTick + context->current->quantumLeft;
    }
    if (context->sleepHead && context->sleepHead->wakeTick < deadline) {
        deadline = context->sleepHead->wakeTick;
    }
    context->nextReschedTick = (deadline == UINT64_MAX) ? 0u : deadline;
}

/*
 * Decide whether the current runner should give the CPU to somebody else.
 *
 * The answer is yes when:
 * - a more important runnable thread is waiting, or
 * - an equally important thread is waiting and the current runner used up its
 *   time slice.
 */
static bool
sp_should_preempt(PSP_CONTEXT context) {
    int bestPriority;

    if (!context || !context->current) {
        return true;
    }

    bestPriority = sp_find_best_priority(context);
    if (bestPriority < 0) {
        return false;
    }
    if ((uint8_t)bestPriority < context->current->currentPriority) {
        return true;
    }
    if ((uint8_t)bestPriority == context->current->currentPriority && context->current->quantumLeft == 0u) {
        return true;
    }
    return false;
}

/*
 * Place one thread on its priority queue after validating that it is alive,
 * resumed, and scheduler-visible.
 */
SP_RESULT
sp_make_thread_runnable(PSP_CONTEXT context, PSP_THREAD thread, bool atTail) {
    SP_READYQUEUE* queue;

    if (!context || !thread || thread->currentPriority > SP_MAX_PRIORITY) {
        return SP_E_INVALID_ARG;
    }
    if (thread->suspendCount) {
        return SP_E_STATE;
    }
    if (thread->state == SP_THREAD_DEAD || thread->state == SP_THREAD_DYING) {
        return SP_E_STATE;
    }

    if (!atTail && thread->quantumLeft > 0u) {
        thread->quantumLeft -= 1u;
        if (thread->quantumLeft == 0u) {
            atTail = true;
            thread->quantumLeft = thread->quantum;
        }
    }
    if (!thread->quantumLeft) {
        thread->quantumLeft = thread->quantum;
    }

    queue = &context->ready[thread->currentPriority];
    thread->state = SP_THREAD_RUNNABLE;
    thread->runnableSince = context->nowTick;
    sp_ready_push(queue, thread, atTail);
    context->readyBitmap |= (1u << thread->currentPriority);
    sp_refresh_deadline(context);
    return SP_OK;
}

/*
 * Choose who runs next.
 *
 * Think of the ready queues like lines for a ride:
 * - smaller priority numbers are more important lines,
 * - the current runner may step back into its line,
 * - then the scheduler picks the best thread at the front of the best line.
 */
SP_RESULT
sp_dispatch(PSP_CONTEXT context, PSP_THREAD* outCurrent) {
    int        bestPriority;
    PSP_THREAD bestThread;

    if (!context) {
        return SP_E_INVALID_ARG;
    }

    bestPriority = sp_find_best_priority(context);
    if (context->current && context->current->state == SP_THREAD_RUNNING && bestPriority >= 0) {
        if ((uint8_t)bestPriority < context->current->currentPriority) {
            /* A more important thread showed up, so the current one goes back to wait. */
            context->current->state = SP_THREAD_RUNNABLE;
            (void)sp_make_thread_runnable(context, context->current, false);
            context->current = NULL;
        }
        else if ((uint8_t)bestPriority == context->current->currentPriority && context->current->quantumLeft == 0u) {
            /* Same priority, but the current runner used its turn, so move it to the tail. */
            context->current->state = SP_THREAD_RUNNABLE;
            context->current->quantumLeft = context->current->quantum;
            (void)sp_make_thread_runnable(context, context->current, true);
            context->current = NULL;
        }
    }

    if (!context->current) {
        if (bestPriority < 0) {
            if (outCurrent) {
                *outCurrent = NULL;
            }
            sp_refresh_deadline(context);
            return SP_E_EMPTY;
        }

        /* Always take the thread at the front of the best non-empty queue. */
        bestThread = sp_ready_pop_head(&context->ready[bestPriority]);
        if (sp_ready_is_empty(&context->ready[bestPriority])) {
            context->readyBitmap &= ~(1u << bestPriority);
        }

        bestThread->state = SP_THREAD_RUNNING;
        if (!bestThread->quantumLeft) {
            bestThread->quantumLeft = bestThread->quantum;
        }
        context->current = bestThread;
    }

    if (outCurrent) {
        *outCurrent = context->current;
    }
    sp_refresh_deadline(context);
    return SP_OK;
}

/*
 * sp_advance_time
 *
 * Shared timebase helper for both scheduler tick entry points.
 *
 * Important behaviour split:
 *   allowDispatch = true:
 *     Full scheduler tick. Advance time, wake sleepers, and immediately run a
 *     dispatch decision when a higher-priority or timeslice-expired thread
 *     should take over.
 *
 *   allowDispatch = false:
 *     Deferred tick. Advance time and wake sleepers, but leave the final
 *     dispatch to a later safe point chosen by the kernel compatibility layer.
 *     This is required when the timer interrupted EL1 code that is still using
 *     the current task's kernel stack and exception frame.
 */
static SP_RESULT
sp_advance_time(PSP_CONTEXT context, uint64_t deltaTicks, bool allowDispatch) {
    if (!context || !deltaTicks) {
        return SP_E_INVALID_ARG;
    }

    context->nowTick += deltaTicks;
    if (context->current && context->current->state == SP_THREAD_RUNNING) {
        context->current->kernelTicks += deltaTicks;
        if (context->current->quantumLeft > deltaTicks) {
            context->current->quantumLeft -= (uint32_t)deltaTicks;
        }
        else {
            context->current->quantumLeft = 0u;
        }
    }

    while (context->sleepHead && context->sleepHead->wakeTick <= context->nowTick) {
        PSP_THREAD waking = context->sleepHead;
        sp_sleep_remove(context, waking);
        waking->state = SP_THREAD_CREATED;
        (void)sp_make_thread_runnable(context, waking, true);
    }

    sp_refresh_deadline(context);
    if (!allowDispatch) {
        return SP_OK;
    }

    if (sp_should_preempt(context)) {
        SP_RESULT result = sp_dispatch(context, NULL);
        if (result == SP_E_EMPTY) {
            return SP_OK;
        }
        return result;
    }
    return SP_OK;
}

/* Advance time and allow the dispatcher to react immediately if needed. */
SP_RESULT
sp_tick(PSP_CONTEXT context, uint64_t deltaTicks) {
    return sp_advance_time(context, deltaTicks, true);
}

/* Advance time without switching away from the current kernel exception frame. */
SP_RESULT
sp_tick_deferred(PSP_CONTEXT context, uint64_t deltaTicks) {
    return sp_advance_time(context, deltaTicks, false);
}

/* Move the current running thread behind its equal-priority peers. */
SP_RESULT
sp_yield_current(PSP_CONTEXT context) {
    if (!context || !context->current || context->current->state != SP_THREAD_RUNNING) {
        return SP_E_STATE;
    }

    context->current->state = SP_THREAD_RUNNABLE;
    context->current->quantumLeft = context->current->quantum;
    (void)sp_make_thread_runnable(context, context->current, true);
    context->current = NULL;
    return sp_dispatch(context, NULL);
}

/* Mark the current thread blocked and immediately dispatch a replacement. */
SP_RESULT
sp_block_current(PSP_CONTEXT context) {
    if (!context || !context->current || context->current->state != SP_THREAD_RUNNING) {
        return SP_E_STATE;
    }

    context->current->state = SP_THREAD_BLOCKED;
    context->current->blockedSince = context->nowTick;
    context->current = NULL;
    sp_refresh_deadline(context);
    return sp_dispatch(context, NULL);
}

/* Move the current thread to the sleep queue until its wake deadline expires. */
SP_RESULT
sp_sleep_current(PSP_CONTEXT context, uint64_t delayTicks) {
    if (!context || !context->current || context->current->state != SP_THREAD_RUNNING || !delayTicks) {
        return SP_E_INVALID_ARG;
    }

    context->current->state = SP_THREAD_SLEEPING;
    context->current->wakeTick = context->nowTick + delayTicks;
    context->current->blockedSince = context->nowTick;
    sp_sleep_insert_sorted(context, context->current);
    context->current = NULL;
    sp_refresh_deadline(context);
    return sp_dispatch(context, NULL);
}

/* Return one blocked or newly-created thread to runnable state. */
SP_RESULT
sp_unblock_thread(PSP_CONTEXT context, PSP_THREAD thread, bool atTail) {
    if (!context || !thread) {
        return SP_E_INVALID_ARG;
    }
    if (thread->state != SP_THREAD_BLOCKED && thread->state != SP_THREAD_CREATED) {
        return SP_E_STATE;
    }

    return sp_make_thread_runnable(context, thread, atTail);
}
