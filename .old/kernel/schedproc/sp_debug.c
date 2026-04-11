/*
 * sp_debug.c
 *
 * Human-readable state dump helpers for the scaffolded module.
 */
#include "../include/sp_internal.h"

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
#include <stdio.h>
#else
#include "printf.h"
#endif

 /*
  * Small local formatter buffer size used by the debug dumper.
  *
  * The dump helper intentionally emits short fragments so the same code can
  * target:
  * - hosted tests, through a `FILE *` bridge, and
  * - the freestanding kernel console, through `kprint`.
  */
#define SP_DEBUG_BUFFER_SIZE 96

static void
sp_emit_text(SP_TEXT_EMIT_FN emit, void* emitContext, const char* text) {
    if (!emit || !text) {
        return;
    }

    emit(emitContext, text);
}

/*
 * Emit a compact one-line scheduler snapshot that shows the current thread,
 * non-empty ready queues, and the next sleeping thread due to wake.
 */
void sp_dump_state(PSP_CONTEXT context, SP_TEXT_EMIT_FN emit, void* emitContext) {
    uint32_t priority;

    if (!context || !emit) {
        return;
    }

    {
        char buffer[SP_DEBUG_BUFFER_SIZE];
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
        (void)snprintf(buffer, sizeof(buffer), "tick=%u ", (unsigned)context->nowTick);
#else
        sprintf(buffer, "tick=%u ", (unsigned)context->nowTick);
#endif
        sp_emit_text(emit, emitContext, buffer);
    }

    if (context->current) {
        char buffer[SP_DEBUG_BUFFER_SIZE];
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "current=%s[p=%u,q=%u,state=%s] ",
            context->current->name,
            (unsigned)context->current->currentPriority,
            (unsigned)context->current->quantumLeft,
            sp_thread_state_name(context->current->state));
#else
        sprintf(
            buffer,
            "current=%s[p=%u,q=%u,state=%s] ",
            context->current->name,
            (unsigned)context->current->currentPriority,
            (unsigned)context->current->quantumLeft,
            sp_thread_state_name(context->current->state));
#endif
        sp_emit_text(emit, emitContext, buffer);
    }
    else {
        sp_emit_text(emit, emitContext, "current=<idle> ");
    }

    sp_emit_text(emit, emitContext, "ready={");
    for (priority = 0u; priority < SP_READY_LEVELS; ++priority) {
        if (context->ready[priority].head) {
            char buffer[SP_DEBUG_BUFFER_SIZE];
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
            (void)snprintf(buffer, sizeof(buffer), "%u:%s ", (unsigned)priority, context->ready[priority].head->name);
#else
            sprintf(buffer, "%u:%s ", (unsigned)priority, context->ready[priority].head->name);
#endif
            sp_emit_text(emit, emitContext, buffer);
        }
    }
    sp_emit_text(emit, emitContext, "} ");

    if (context->sleepHead) {
        char buffer[SP_DEBUG_BUFFER_SIZE];
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
        (void)snprintf(
            buffer,
            sizeof(buffer),
            "sleep_head=%s@%u",
            context->sleepHead->name,
            (unsigned)context->sleepHead->wakeTick);
#else
        sprintf(
            buffer,
            "sleep_head=%s@%u",
            context->sleepHead->name,
            (unsigned)context->sleepHead->wakeTick);
#endif
        sp_emit_text(emit, emitContext, buffer);
    }
    else {
        sp_emit_text(emit, emitContext, "sleep_head=<none>");
    }
    sp_emit_text(emit, emitContext, "\n");
}
