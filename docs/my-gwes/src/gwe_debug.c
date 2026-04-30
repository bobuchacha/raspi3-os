/*
 * gwe_debug.c
 *
 * Human-readable dump helpers for the GWES scaffold.
 */
#include "../include/gwe_internal.h"

void
gwe_dump_state(PCGWE_CONTEXT context, FILE *stream) {
    PCGWE_THREAD_QUEUE queue;
    PCGWE_WINDOW window;

    if (!stream) {
        return;
    }
    if (!context) {
        fprintf(stream, "gwe: <null context>\n");
        return;
    }

    fprintf(stream,
            "desktop: %dx%d windows=%zu focus=%p active=%p capture=%p presents=%llu damage=%u\n",
            context->desktopWidth,
            context->desktopHeight,
            gwe_count_windows(context),
            (void *)context->focusWindow,
            (void *)context->activeWindow,
            (void *)context->captureWindow,
            (unsigned long long)context->presentCount,
            context->desktopDamage.valid ? 1u : 0u);

    fprintf(stream, "queues:\n");
    for (queue = context->queueHead; queue; queue = queue->nextQueue) {
        fprintf(stream,
                "  tid=%u queued=%zu quit=%u\n",
                (unsigned)queue->threadId,
                queue->postedCount,
                queue->quitPosted ? 1u : 0u);
    }

    fprintf(stream, "windows:\n");
    for (window = context->windowHead; window; window = window->nextWindow) {
        fprintf(stream,
                "  #%u %s tid=%u rect=%d,%d %dx%d flags=%u paint=%llu dispatch=%llu invalid=%u\n",
                (unsigned)window->serial,
                window->name,
                (unsigned)window->ownerTid,
                window->windowRect.x,
                window->windowRect.y,
                window->windowRect.w,
                window->windowRect.h,
                (unsigned)window->flags,
                (unsigned long long)window->paintCount,
                (unsigned long long)window->dispatchCount,
                window->invalidRegion.valid ? 1u : 0u);
    }
}
