/*
 * gwe_message.c
 *
 * Per-thread GUI queue handling and message dispatch.
 */
#include "../include/gwe_internal.h"

#include <string.h>

GWE_RESULT
gwe_queue_message(PGWE_CONTEXT context, PGWE_THREAD_QUEUE queue, const GWE_MESSAGE *message) {
    GWE_MESSAGENODE *node;

    if (!context || !queue || !message) {
        return GWE_E_INVALID_ARG;
    }

    node = (GWE_MESSAGENODE *)gwe_alloc(context, sizeof(*node));
    if (!node) {
        return GWE_E_OOM;
    }

    node->message = *message;
    if (!queue->tail) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->postedCount += 1u;
    return GWE_OK;
}

int64_t
gwe_call_window_proc(PGWE_CONTEXT context, PGWE_WINDOW window, PCGWE_MESSAGE message) {
    if (!window || !message) {
        return 0;
    }

    window->dispatchCount += 1u;
    if (message->type == GWE_MSG_PAINT) {
        window->paintCount += 1u;
    }

    if (!window->proc) {
        return 0;
    }
    return window->proc(context, window, message, window->userData);
}

void
gwe_drop_messages_for_window(PGWE_CONTEXT context, PGWE_WINDOW window) {
    PGWE_THREAD_QUEUE queue;

    if (!context || !window) {
        return;
    }

    for (queue = context->queueHead; queue; queue = queue->nextQueue) {
        GWE_MESSAGENODE *prev = NULL;
        GWE_MESSAGENODE *node = queue->head;

        while (node) {
            GWE_MESSAGENODE *next = node->next;
            if (node->message.targetWindow == window) {
                if (prev) {
                    prev->next = next;
                } else {
                    queue->head = next;
                }
                if (queue->tail == node) {
                    queue->tail = prev;
                }
                queue->postedCount -= 1u;
                gwe_free(context, node);
            } else {
                prev = node;
            }
            node = next;
        }
    }
}

GWE_RESULT
gwe_post_message(PGWE_CONTEXT context,
                 PGWE_WINDOW window,
                 GWE_MESSAGE_TYPE type,
                 uint64_t wparam,
                 int64_t lparam) {
    PGWE_THREAD_QUEUE queue;
    GWE_MESSAGE message;

    if (!context || !window || !window->ownerTid) {
        return GWE_E_INVALID_ARG;
    }

    queue = gwe_lookup_queue(context, window->ownerTid, true);
    if (!queue) {
        return GWE_E_OOM;
    }

    memset(&message, 0, sizeof(message));
    message.type = type;
    message.targetWindow = window;
    message.wparam = wparam;
    message.lparam = lparam;
    message.timeMs = gwe_now(context);
    return gwe_queue_message(context, queue, &message);
}

GWE_RESULT
gwe_post_quit(PGWE_CONTEXT context, uint32_t threadId, int64_t exitCode) {
    PGWE_THREAD_QUEUE queue;
    GWE_MESSAGE message;

    if (!context || !threadId) {
        return GWE_E_INVALID_ARG;
    }

    queue = gwe_lookup_queue(context, threadId, true);
    if (!queue) {
        return GWE_E_OOM;
    }

    memset(&message, 0, sizeof(message));
    message.type = GWE_MSG_QUIT;
    message.lparam = exitCode;
    message.timeMs = gwe_now(context);
    queue->quitPosted = true;
    return gwe_queue_message(context, queue, &message);
}


GWE_RESULT
gwe_send_message(PGWE_CONTEXT context,
                 uint32_t callerTid,
                 PGWE_WINDOW window,
                 GWE_MESSAGE_TYPE type,
                 uint64_t wparam,
                 int64_t lparam,
                 int64_t *outResult) {
    GWE_MESSAGE message;
    (void)callerTid;

    if (!context || !window || !outResult) {
        return GWE_E_INVALID_ARG;
    }

    memset(&message, 0, sizeof(message));
    message.type = type;
    message.targetWindow = window;
    message.wparam = wparam;
    message.lparam = lparam;
    message.timeMs = gwe_now(context);

    *outResult = gwe_call_window_proc(context, window, &message);
    return GWE_OK;
}

GWE_RESULT
gwe_get_message(PGWE_CONTEXT context, uint32_t threadId, PGWE_MESSAGE outMessage) {
    PGWE_THREAD_QUEUE queue;
    GWE_MESSAGENODE *node;

    if (!context || !threadId || !outMessage) {
        return GWE_E_INVALID_ARG;
    }

    queue = gwe_lookup_queue(context, threadId, false);
    if (!queue) {
        return GWE_E_NOT_FOUND;
    }
    if (!queue->head) {
        return GWE_E_EMPTY;
    }

    node = queue->head;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->postedCount -= 1u;
    *outMessage = node->message;
    gwe_free(context, node);
    return GWE_OK;
}

GWE_RESULT
gwe_dispatch_message(PGWE_CONTEXT context, PCGWE_MESSAGE message, int64_t *outResult) {
    if (!context || !message || !outResult) {
        return GWE_E_INVALID_ARG;
    }

    if (message->type == GWE_MSG_QUIT) {
        *outResult = message->lparam;
        return GWE_OK;
    }
    if (!message->targetWindow) {
        return GWE_E_INVALID_ARG;
    }

    *outResult = gwe_call_window_proc(context, message->targetWindow, message);
    return GWE_OK;
}