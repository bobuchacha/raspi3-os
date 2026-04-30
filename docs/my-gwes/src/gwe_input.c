/*
 * gwe_input.c
 *
 * Pointer and keyboard routing for the GWES scaffold.
 */
#include "../include/gwe_internal.h"

#include <string.h>

static void
gwe_update_focus_and_activation(PGWE_CONTEXT context, PGWE_WINDOW window) {
    if (!context || !window) {
        return;
    }

    context->focusWindow = window;
    context->activeWindow = window;
}

GWE_RESULT
gwe_dispatch_pointer(PGWE_CONTEXT context,
                     GWE_POINTER_KIND kind,
                     int32_t x,
                     int32_t y,
                     uint32_t buttons) {
    PGWE_WINDOW target;
    PGWE_THREAD_QUEUE queue;
    GWE_MESSAGE message;
    GWE_POINT point;

    if (!context) {
        return GWE_E_INVALID_ARG;
    }

    point.x = x;
    point.y = y;
    target = context->captureWindow ? context->captureWindow : gwe_hit_test_topmost(context, point);
    if (!target) {
        return GWE_OK;
    }

    if (kind == GWE_POINTER_DOWN) {
        gwe_update_focus_and_activation(context, target);
    }

    queue = gwe_lookup_queue(context, target->ownerTid, true);
    if (!queue) {
        return GWE_E_OOM;
    }

    memset(&message, 0, sizeof(message));
    message.type = (kind == GWE_POINTER_DOWN)
                       ? GWE_MSG_POINTER_DOWN
                       : ((kind == GWE_POINTER_MOVE) ? GWE_MSG_POINTER_MOVE : GWE_MSG_POINTER_UP);
    message.targetWindow = target;
    message.wparam = buttons;
    message.timeMs = gwe_now(context);
    message.screenPoint = point;
    message.clientPoint.x = x - target->windowRect.x;
    message.clientPoint.y = y - target->windowRect.y;
    return gwe_queue_message(context, queue, &message);
}

GWE_RESULT
gwe_dispatch_key(PGWE_CONTEXT context,
                 bool keyDown,
                 uint32_t scanCode,
                 uint32_t keyCode) {
    PGWE_WINDOW target;
    PGWE_THREAD_QUEUE queue;
    GWE_MESSAGE message;

    if (!context) {
        return GWE_E_INVALID_ARG;
    }

    target = context->focusWindow ? context->focusWindow : context->activeWindow;
    if (!target) {
        return GWE_E_NOT_FOUND;
    }

    queue = gwe_lookup_queue(context, target->ownerTid, true);
    if (!queue) {
        return GWE_E_OOM;
    }

    memset(&message, 0, sizeof(message));
    message.type = keyDown ? GWE_MSG_KEY_DOWN : GWE_MSG_KEY_UP;
    message.targetWindow = target;
    message.wparam = keyCode;
    message.lparam = (int64_t)scanCode;
    message.timeMs = gwe_now(context);
    return gwe_queue_message(context, queue, &message);
}
