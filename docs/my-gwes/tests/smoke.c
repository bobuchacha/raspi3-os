/*
 * smoke.c
 *
 * Host-side smoke test for the my-gwes scaffold.
 */
#include "../include/gwe_debug.h"
#include "../include/gwe_graphics.h"
#include "../include/gwe_input.h"
#include "../include/gwe_message.h"
#include "../include/gwe_module.h"
#include "../include/gwe_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct WindowStateStruct {
    uint32_t createCount;
    uint32_t pointerDownCount;
    uint32_t paintCount;
    GWE_POINT lastClientPoint;
    uint32_t fillColor;
} WINDOW_STATE;

static void
require_ok(GWE_RESULT result, const char *step) {
    if (result != GWE_OK) {
        fprintf(stderr, "step failed: %s (result=%d)\n", step, (int)result);
        exit(1);
    }
}

static void
require_true(int condition, const char *step) {
    if (!condition) {
        fprintf(stderr, "assertion failed: %s\n", step);
        exit(1);
    }
}

static int64_t
test_window_proc(PGWE_CONTEXT context, PGWE_WINDOW window, PCGWE_MESSAGE message, void *userData) {
    WINDOW_STATE *state = (WINDOW_STATE *)userData;

    switch (message->type) {
    case GWE_MSG_CREATE:
        state->createCount += 1u;
        return 1;

    case GWE_MSG_PAINT: {
        GWE_PAINT_CONTEXT paint;
        GWE_RECT fillRect;

        state->paintCount += 1u;
        require_ok(gwe_begin_paint(context, window, &paint), "gwe_begin_paint");
        fillRect = paint.clip.bounds;
        require_ok(gwe_fill_paint_rect(&paint, &fillRect, state->fillColor), "gwe_fill_paint_rect");
        require_ok(gwe_end_paint(context, &paint), "gwe_end_paint");
        return 2;
    }

    case GWE_MSG_POINTER_DOWN:
        state->pointerDownCount += 1u;
        state->lastClientPoint = message->clientPoint;
        return (int64_t)(message->clientPoint.x + message->clientPoint.y);

    case GWE_MSG_USER:
        return (int64_t)(message->wparam + (uint64_t)message->lparam + state->fillColor);

    default:
        return 0;
    }
}

static void
drain_one(PGWE_CONTEXT context, uint32_t tid, GWE_MESSAGE_TYPE expectedType, PGWE_WINDOW expectedWindow) {
    GWE_MESSAGE message;
    int64_t result;

    require_ok(gwe_get_message(context, tid, &message), "gwe_get_message");
    require_true(message.type == expectedType, "expected message type");
    require_true(message.targetWindow == expectedWindow, "expected message window");
    require_ok(gwe_dispatch_message(context, &message, &result), "gwe_dispatch_message");
}

int
main(void) {
    GWE_INITINFO initInfo;
    PGWE_CONTEXT context = NULL;
    PGWE_WINDOW windowA = NULL;
    PGWE_WINDOW windowB = NULL;
    WINDOW_STATE stateA;
    WINDOW_STATE stateB;
    GWE_WINDOW_CREATEINFO createInfo;
    int64_t result;
    uint32_t pixel;
    size_t composedCount = 0u;
    GWE_DESKTOPINFO desktopInfo;
    GWE_THREADQUEUEINFO queueInfo;
    GWE_MESSAGE quitMessage;

    memset(&stateA, 0, sizeof(stateA));
    memset(&stateB, 0, sizeof(stateB));
    stateA.fillColor = 0xff204060u;
    stateB.fillColor = 0xffb04020u;

    initInfo.desktopWidth = 160;
    initInfo.desktopHeight = 120;
    initInfo.compositorEnabled = true;

    require_ok(gwe_init(NULL, &initInfo, &context), "gwe_init");
    require_ok(gwe_register_thread(context, 1u), "gwe_register_thread(1)");
    require_ok(gwe_register_thread(context, 2u), "gwe_register_thread(2)");

    memset(&createInfo, 0, sizeof(createInfo));
    createInfo.ownerTid = 1u;
    createInfo.rect.x = 0;
    createInfo.rect.y = 0;
    createInfo.rect.w = 60;
    createInfo.rect.h = 40;
    createInfo.proc = test_window_proc;
    createInfo.userData = &stateA;
    createInfo.name = "Alpha";
    require_ok(gwe_create_window(context, &createInfo, &windowA), "gwe_create_window(A)");

    memset(&createInfo, 0, sizeof(createInfo));
    createInfo.ownerTid = 2u;
    createInfo.rect.x = 20;
    createInfo.rect.y = 20;
    createInfo.rect.w = 50;
    createInfo.rect.h = 40;
    createInfo.proc = test_window_proc;
    createInfo.userData = &stateB;
    createInfo.name = "Bravo";
    require_ok(gwe_create_window(context, &createInfo, &windowB), "gwe_create_window(B)");

    drain_one(context, 1u, GWE_MSG_CREATE, windowA);
    drain_one(context, 2u, GWE_MSG_CREATE, windowB);
    require_true(stateA.createCount == 1u, "window A create count");
    require_true(stateB.createCount == 1u, "window B create count");

    require_ok(gwe_send_message(context, 99u, windowB, GWE_MSG_USER, 5u, 7, &result), "gwe_send_message");
    require_true(result == (int64_t)(5u + 7u + stateB.fillColor), "send message result");

    require_ok(gwe_invalidate_rect(context, windowA, NULL), "gwe_invalidate_rect(A)");
    require_ok(gwe_invalidate_rect(context, windowB, NULL), "gwe_invalidate_rect(B)");
    drain_one(context, 1u, GWE_MSG_PAINT, windowA);
    drain_one(context, 2u, GWE_MSG_PAINT, windowB);
    require_true(stateA.paintCount == 1u, "window A paint count");
    require_true(stateB.paintCount == 1u, "window B paint count");

    require_ok(gwe_read_window_pixel(windowA, 5, 5, &pixel), "gwe_read_window_pixel(A)");
    require_true(pixel == stateA.fillColor, "window A paint color");
    require_ok(gwe_read_window_pixel(windowB, 5, 5, &pixel), "gwe_read_window_pixel(B)");
    require_true(pixel == stateB.fillColor, "window B paint color");

    require_ok(gwe_dispatch_pointer(context, GWE_POINTER_DOWN, 30, 30, 1u), "gwe_dispatch_pointer");
    drain_one(context, 2u, GWE_MSG_POINTER_DOWN, windowB);
    require_true(stateB.pointerDownCount == 1u, "window B pointer count");
    require_true(stateB.lastClientPoint.x == 10 && stateB.lastClientPoint.y == 10, "window B client point");

    require_ok(gwe_query_desktop(context, &desktopInfo), "gwe_query_desktop(before compose)");
    require_true(desktopInfo.activeWindow == windowB, "desktop active window");
    require_true(desktopInfo.focusWindow == windowB, "desktop focus window");
    require_true(desktopInfo.damage.valid, "desktop damage pending");

    require_ok(gwe_compose(context, &composedCount), "gwe_compose");
    require_true(composedCount == 2u, "compose count");
    require_ok(gwe_read_desktop_pixel(context, 10, 10, &pixel), "gwe_read_desktop_pixel(a)");
    require_true(pixel == stateA.fillColor, "desktop pixel from window A");
    require_ok(gwe_read_desktop_pixel(context, 30, 30, &pixel), "gwe_read_desktop_pixel(b)");
    require_true(pixel == stateB.fillColor, "desktop overlap pixel from top window B");

    require_ok(gwe_query_desktop(context, &desktopInfo), "gwe_query_desktop(after compose)");
    require_true(!desktopInfo.damage.valid, "desktop damage cleared");
    require_true(desktopInfo.presentCount == 1u, "desktop present count");

    require_ok(gwe_query_thread_queue(context, 1u, &queueInfo), "gwe_query_thread_queue");
    require_true(queueInfo.postedCount == 0u, "thread 1 queue drained");

    require_ok(gwe_post_quit(context, 1u, 123), "gwe_post_quit");
    require_ok(gwe_get_message(context, 1u, &quitMessage), "gwe_get_message(quit)");
    require_true(quitMessage.type == GWE_MSG_QUIT && quitMessage.lparam == 123, "quit message fields");

    puts("== gwe state ==");
    gwe_dump_state(context, stdout);

    require_ok(gwe_destroy_window(context, windowB), "gwe_destroy_window(B)");
    require_ok(gwe_destroy_window(context, windowA), "gwe_destroy_window(A)");
    require_ok(gwe_unregister_thread(context, 2u), "gwe_unregister_thread(2)");
    require_ok(gwe_unregister_thread(context, 1u), "gwe_unregister_thread(1)");
    require_ok(gwe_shutdown(context), "gwe_shutdown");
    return 0;
}
