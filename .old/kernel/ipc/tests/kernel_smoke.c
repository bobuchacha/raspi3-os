/*
 * kernel_smoke.c
 *
 * Kernel-side smoke coverage for the IPC module.
 *
 * This suite intentionally uses the live kernel-owned IPC context instead of a
 * private test context. That verifies the module is actually wired into the
 * running kernel and can service ordinary allocator, timer, and logging calls.
 */
#include "../../include/ipc_event.h"
#include "../../include/ipc_kernel.h"
#include "../../include/ipc_mailbox.h"
#include "../../include/ipc_mapping.h"

#include "../../include/log.h"

#include <string.h>

 /* Return a stable failure code and emit the failing step once. */
static int ipc_kernel_expect_ok(IPC_RESULT result, const char* step) {
    if (result != IPC_OK) {
        log_fail("ipc smoke: %s failed (result=%d)", step, (int)result);
        return -1;
    }

    return 0;
}

/* Emit an assertion-style failure that is easy to spot in boot logs. */
static int ipc_kernel_expect_true(int condition, const char* step) {
    if (!condition) {
        log_fail("ipc smoke: assertion failed: %s", step);
        return -1;
    }

    return 0;
}

/* Exercise the live IPC singleton with one event, mapping, and mailbox flow. */
int ipc_kernel_smoke_test_main(void) {
    PIPC_CONTEXT context = ipc_kernel_context();
    PIPC_EVENT event = NULL;
    PIPC_EVENT event_alias = NULL;
    PIPC_MAPPING mapping = NULL;
    PIPC_MAILBOX mailbox = NULL;
    IPC_EVENTINFO event_info;
    IPC_MAPPINGINFO mapping_info;
    IPC_MAILBOXINFO mailbox_info;
    void* view = NULL;
    size_t view_size = 0u;
    char mapping_buffer[32];
    char recv_buffer[32];
    size_t recv_length = 0u;

    if (!context) {
        log_fail("ipc smoke: kernel IPC context is not initialized");
        return -1;
    }

    if (ipc_kernel_expect_ok(ipc_create_event(context, "kernel.smoke.ready", false, false, &event), "ipc_create_event") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_open_event(context, "kernel.smoke.ready", &event_alias), "ipc_open_event") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_set_event(context, event), "ipc_set_event") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_wait_event(context, event_alias, 0u), "ipc_wait_event") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(ipc_wait_event(context, event_alias, 0u) == IPC_E_TIMEOUT,
        "auto-reset event should time out on second wait") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_query_event(event, &event_info), "ipc_query_event") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(event_info.setCount == 1u, "event set count should be one") != 0) {
        return -1;
    }

    if (ipc_kernel_expect_ok(ipc_create_mapping(context, "kernel.smoke.map", 128u, &mapping), "ipc_create_mapping") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_map_view(mapping, &view, &view_size), "ipc_map_view") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(view != NULL && view_size == 128u, "mapping view should expose full region") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_write_mapping(mapping, 0u, "hello ipc", 10u), "ipc_write_mapping") != 0) {
        return -1;
    }
    memset(mapping_buffer, 0, sizeof(mapping_buffer));
    if (ipc_kernel_expect_ok(ipc_read_mapping(mapping, 0u, mapping_buffer, 10u), "ipc_read_mapping") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(strcmp(mapping_buffer, "hello ipc") == 0, "mapping payload should round-trip") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_query_mapping(mapping, &mapping_info), "ipc_query_mapping") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(mapping_info.sequence == 1u, "mapping sequence should increment") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_unmap_view(mapping, view), "ipc_unmap_view") != 0) {
        return -1;
    }

    if (ipc_kernel_expect_ok(ipc_create_mailbox(context, "kernel.smoke.mailbox", 32u, 4u, &mailbox), "ipc_create_mailbox") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_send_mailbox(context, mailbox, "msg-a", 6u), "ipc_send_mailbox(a)") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_send_mailbox(context, mailbox, "msg-b", 6u), "ipc_send_mailbox(b)") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_query_mailbox(mailbox, &mailbox_info), "ipc_query_mailbox") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(mailbox_info.messageCount == 2u, "mailbox should queue two messages") != 0) {
        return -1;
    }

    memset(recv_buffer, 0, sizeof(recv_buffer));
    if (ipc_kernel_expect_ok(ipc_receive_mailbox(context, mailbox, recv_buffer, sizeof(recv_buffer), &recv_length),
        "ipc_receive_mailbox(a)") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(recv_length == 6u && strcmp(recv_buffer, "msg-a") == 0,
        "first mailbox payload should round-trip") != 0) {
        return -1;
    }

    memset(recv_buffer, 0, sizeof(recv_buffer));
    if (ipc_kernel_expect_ok(ipc_receive_mailbox(context, mailbox, recv_buffer, sizeof(recv_buffer), &recv_length),
        "ipc_receive_mailbox(b)") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_true(strcmp(recv_buffer, "msg-b") == 0, "second mailbox payload should round-trip") != 0) {
        return -1;
    }

    log_info("ipc smoke: event=%s set_count=%u mapping_seq=%u mailbox_depth=%u",
        event_info.name,
        (unsigned)event_info.setCount,
        (unsigned)mapping_info.sequence,
        (unsigned)mailbox_info.highWatermark);

    if (ipc_kernel_expect_ok(ipc_close_event(context, event_alias), "ipc_close_event(alias)") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_close_event(context, event), "ipc_close_event") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_close_mapping(context, mapping), "ipc_close_mapping") != 0) {
        return -1;
    }
    if (ipc_kernel_expect_ok(ipc_close_mailbox(context, mailbox), "ipc_close_mailbox") != 0) {
        return -1;
    }

    return 0;
}