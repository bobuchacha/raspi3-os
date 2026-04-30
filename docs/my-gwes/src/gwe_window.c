/*
 * gwe_window.c
 *
 * Window creation, destruction, query, and capture management.
 */
#include "../include/gwe_internal.h"

static void
gwe_unlink_window(PGWE_CONTEXT context, PGWE_WINDOW window) {
    PGWE_WINDOW *link;

    for (link = &context->windowHead; *link; link = &(*link)->nextWindow) {
        if (*link == window) {
            *link = window->nextWindow;
            window->nextWindow = NULL;
            return;
        }
    }
}

GWE_RESULT
gwe_create_window(PGWE_CONTEXT context, const GWE_WINDOW_CREATEINFO *createInfo, PGWE_WINDOW *outWindow) {
    PGWE_WINDOW window;
    PGWE_WINDOW parent;
    uint32_t flags;

    if (!context || !createInfo || !outWindow || !createInfo->ownerTid ||
        createInfo->rect.w <= 0 || createInfo->rect.h <= 0) {
        return GWE_E_INVALID_ARG;
    }

    if (!gwe_lookup_queue(context, createInfo->ownerTid, true)) {
        return GWE_E_OOM;
    }

    window = (PGWE_WINDOW)gwe_alloc(context, sizeof(*window));
    if (!window) {
        return GWE_E_OOM;
    }

    window->surface = gwe_create_surface(context, createInfo->rect.w, createInfo->rect.h);
    if (!window->surface) {
        gwe_free(context, window);
        return GWE_E_OOM;
    }

    flags = createInfo->flags ? createInfo->flags : (GWE_WINDOW_VISIBLE | GWE_WINDOW_ENABLED);
    if (!createInfo->parent) {
        flags |= GWE_WINDOW_TOPLEVEL;
    }

    window->serial = context->nextWindowSerial++;
    gwe_copy_name(window->name, createInfo->name ? createInfo->name : "Window");
    window->ownerTid = createInfo->ownerTid;
    window->windowRect = createInfo->rect;
    window->clientRect.x = 0;
    window->clientRect.y = 0;
    window->clientRect.w = createInfo->rect.w;
    window->clientRect.h = createInfo->rect.h;
    window->flags = flags;
    window->style = createInfo->style;
    window->exStyle = createInfo->exStyle;
    window->proc = createInfo->proc;
    window->userData = createInfo->userData;
    gwe_region_clear(&window->invalidRegion);

    parent = createInfo->parent ? createInfo->parent : context->desktopRoot;
    gwe_attach_child(parent, window);

    window->nextWindow = context->windowHead;
    context->windowHead = window;
    gwe_insert_z_top(context, window);

    *outWindow = window;
    return gwe_post_message(context, window, GWE_MSG_CREATE, 0u, 0);
}

GWE_RESULT
gwe_destroy_window(PGWE_CONTEXT context, PGWE_WINDOW window) {
    int64_t ignoredResult;

    if (!context || !window) {
        return GWE_E_INVALID_ARG;
    }

    while (window->firstChild) {
        GWE_RESULT result = gwe_destroy_window(context, window->firstChild);
        if (result != GWE_OK) {
            return result;
        }
    }

    window->flags |= GWE_WINDOW_DESTROY_PENDING;
    gwe_drop_messages_for_window(context, window);

    if (context->focusWindow == window) {
        context->focusWindow = NULL;
    }
    if (context->activeWindow == window) {
        context->activeWindow = NULL;
    }
    if (context->captureWindow == window) {
        context->captureWindow = NULL;
    }

    if (window->proc) {
        GWE_MESSAGE destroyMessage;
        memset(&destroyMessage, 0, sizeof(destroyMessage));
        destroyMessage.type = GWE_MSG_DESTROY;
        destroyMessage.targetWindow = window;
        destroyMessage.timeMs = gwe_now(context);
        ignoredResult = gwe_call_window_proc(context, window, &destroyMessage);
        (void)ignoredResult;
    }

    if (window->parent) {
        gwe_detach_child(window->parent, window);
    }
    gwe_remove_z(context, window);
    gwe_unlink_window(context, window);
    gwe_destroy_surface(context, window->surface);
    gwe_free(context, window);
    return GWE_OK;
}

GWE_RESULT
gwe_query_window(PGWE_WINDOW window, GWE_WINDOWINFO *outInfo) {
    if (!window || !outInfo) {
        return GWE_E_INVALID_ARG;
    }

    outInfo->serial = window->serial;
    gwe_copy_name(outInfo->name, window->name);
    outInfo->ownerTid = window->ownerTid;
    outInfo->windowRect = window->windowRect;
    outInfo->clientRect = window->clientRect;
    outInfo->invalidRegion = window->invalidRegion;
    outInfo->flags = window->flags;
    outInfo->style = window->style;
    outInfo->exStyle = window->exStyle;
    outInfo->dispatchCount = window->dispatchCount;
    outInfo->paintCount = window->paintCount;
    outInfo->hasSurface = (window->surface != NULL);
    return GWE_OK;
}

GWE_RESULT
gwe_set_capture(PGWE_CONTEXT context, PGWE_WINDOW window) {
    if (!context || !window) {
        return GWE_E_INVALID_ARG;
    }
    context->captureWindow = window;
    return GWE_OK;
}

GWE_RESULT
gwe_release_capture(PGWE_CONTEXT context, PGWE_WINDOW window) {
    if (!context || !window) {
        return GWE_E_INVALID_ARG;
    }
    if (context->captureWindow == window) {
        context->captureWindow = NULL;
    }
    return GWE_OK;
}
