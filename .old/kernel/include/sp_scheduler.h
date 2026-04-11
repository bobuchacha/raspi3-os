/*
 * sp_scheduler.h
 *
 * Public scheduler API.
 */
#ifndef SP_SCHEDULER_H
#define SP_SCHEDULER_H

#include "sp_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic text emitter used by `sp_dump_state`.
 *
 * The scheduler module stays usable in both:
 * - freestanding kernel builds, where stdio is unavailable, and
 * - hosted smoke tests, where callers can still bridge to `stdout`.
 *
 * Args:
 *   context: Caller-owned sink state forwarded back on every callback.
 *   text: NUL-terminated fragment to append to the destination.
 *
 * Returns:
 *   Nothing. The callback is expected to consume the whole fragment.
 */
typedef void (*SP_TEXT_EMIT_FN)(void *context, const char *text);

/* Place one thread on a ready queue. */
SP_RESULT sp_make_thread_runnable(PSP_CONTEXT context, PSP_THREAD thread, bool atTail);

/* Run one dispatch decision and optionally return the selected current thread. */
SP_RESULT sp_dispatch(PSP_CONTEXT context, PSP_THREAD *outCurrent);

/* Advance scheduler time and wake sleepers as needed. */
SP_RESULT sp_tick(PSP_CONTEXT context, uint64_t deltaTicks);

/*
 * Advance scheduler time without performing an immediate dispatch decision.
 *
 * This is used by the kernel compatibility layer when a timer interrupt fires
 * while the CPU is already handling EL1 code. In that situation the scheduler
 * still needs to:
 * - move time forward,
 * - debit the running thread's quantum budget,
 * - and wake sleeping threads,
 *
 * but it must not replace the currently executing kernel thread underneath the
 * active exception frame. The caller will perform a normal schedule later at a
 * safe point.
 */
SP_RESULT sp_tick_deferred(PSP_CONTEXT context, uint64_t deltaTicks);

/* Yield the current thread behind equal-priority peers. */
SP_RESULT sp_yield_current(PSP_CONTEXT context);

/* Block the current thread until explicitly unblocked. */
SP_RESULT sp_block_current(PSP_CONTEXT context);

/* Put the current thread to sleep for delayTicks. */
SP_RESULT sp_sleep_current(PSP_CONTEXT context, uint64_t delayTicks);

/* Unblock one blocked thread and requeue it. */
SP_RESULT sp_unblock_thread(PSP_CONTEXT context, PSP_THREAD thread, bool atTail);

/*
 * Dump one compact scheduler snapshot through a caller-supplied text emitter.
 *
 * Args:
 *   context: Scheduler context to inspect.
 *   emit: Output callback that receives short text fragments.
 *   emitContext: Opaque value passed back into `emit`.
 *
 * Returns:
 *   Nothing. Invalid arguments are ignored.
 */
void sp_dump_state(PSP_CONTEXT context, SP_TEXT_EMIT_FN emit, void *emitContext);

#ifdef __cplusplus
}
#endif

#endif /* SP_SCHEDULER_H */
