/*
 * smoke.c
 *
 * Host-side smoke test for the IPC scaffold.
 */
#include "../include/ipc_debug.h"
#include "../include/ipc_event.h"
#include "../include/ipc_mailbox.h"
#include "../include/ipc_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
require_ok(IPC_RESULT result, const char* step) {
    if (result != IPC_OK) {
        fprintf(stderr, "step failed: %s (result=%d)\n", step, (int)result);
        exit(1);
    }
}

static void
require_true(int condition, const char* step) {
    if (!condition) {
        fprintf(stderr, "assertion failed: %s\n", step);
        exit(1);
    }
}

int test_ipc_main(void) {
    PIPC_CONTEXT    context = NULL;
    PIPC_EVENT      event = NULL;
    PIPC_EVENT      eventAlias = NULL;
    PIPC_MAPPING    mapping = NULL;
    PIPC_MAILBOX    mailbox = NULL;
    IPC_EVENTINFO   eventInfo;
    IPC_MAPPINGINFO mappingInfo;
    IPC_MAILBOXINFO mailboxInfo;
    void* view = NULL;
    size_t          viewSize = 0u;
    char            mappingBuffer[64];
    char            recvBuffer[32];
    size_t          recvLength = 0u;

    require_ok(ipc_init(NULL, &context), "ipc_init");

    require_ok(ipc_create_event(context, "shell.ready", false, false, &event), "ipc_create_event");
    require_ok(ipc_open_event(context, "shell.ready", &eventAlias), "ipc_open_event");
    require_ok(ipc_set_event(context, event), "ipc_set_event");
    require_ok(ipc_wait_event(context, eventAlias, 0u), "ipc_wait_event(auto-reset)");
    require_true(ipc_wait_event(context, eventAlias, 0u) == IPC_E_TIMEOUT, "second auto-reset wait should timeout");
    require_ok(ipc_query_event(event, &eventInfo), "ipc_query_event");
    require_true(eventInfo.setCount == 1u, "event set count");

    require_ok(ipc_create_mapping(context, "proc.shared", 128u, &mapping), "ipc_create_mapping");
    require_ok(ipc_map_view(mapping, &view, &viewSize), "ipc_map_view");
    require_true(view != NULL && viewSize == 128u, "mapping view returned");
    require_ok(ipc_write_mapping(mapping, 0u, "hello ipc", 10u), "ipc_write_mapping");
    memset(mappingBuffer, 0, sizeof(mappingBuffer));
    require_ok(ipc_read_mapping(mapping, 0u, mappingBuffer, 10u), "ipc_read_mapping");
    require_true(strcmp(mappingBuffer, "hello ipc") == 0, "mapping payload");
    require_ok(ipc_query_mapping(mapping, &mappingInfo), "ipc_query_mapping");
    require_true(mappingInfo.sequence == 1u, "mapping sequence increment");
    require_ok(ipc_unmap_view(mapping, view), "ipc_unmap_view");

    require_ok(ipc_create_mailbox(context, "kernel.notify", 32u, 4u, &mailbox), "ipc_create_mailbox");
    require_ok(ipc_send_mailbox(context, mailbox, "msg-a", 6u), "ipc_send_mailbox(a)");
    require_ok(ipc_send_mailbox(context, mailbox, "msg-b", 6u), "ipc_send_mailbox(b)");
    require_ok(ipc_query_mailbox(mailbox, &mailboxInfo), "ipc_query_mailbox");
    require_true(mailboxInfo.messageCount == 2u, "mailbox queued count");
    require_true(mailboxInfo.highWatermark == 2u, "mailbox high watermark");

    memset(recvBuffer, 0, sizeof(recvBuffer));
    require_ok(ipc_receive_mailbox(context, mailbox, recvBuffer, sizeof(recvBuffer), &recvLength), "ipc_receive_mailbox(a)");
    require_true(recvLength == 6u, "mailbox recv length a");
    require_true(strcmp(recvBuffer, "msg-a") == 0, "mailbox payload a");

    memset(recvBuffer, 0, sizeof(recvBuffer));
    require_ok(ipc_receive_mailbox(context, mailbox, recvBuffer, sizeof(recvBuffer), &recvLength), "ipc_receive_mailbox(b)");
    require_true(strcmp(recvBuffer, "msg-b") == 0, "mailbox payload b");

    puts("== ipc state ==");
    ipc_dump_state(context, stdout);

    require_ok(ipc_close_event(context, eventAlias), "ipc_close_event(alias)");
    require_ok(ipc_close_event(context, event), "ipc_close_event");
    require_ok(ipc_close_mapping(context, mapping), "ipc_close_mapping");
    require_ok(ipc_close_mailbox(context, mailbox), "ipc_close_mailbox");
    require_ok(ipc_shutdown(context), "ipc_shutdown");
    return 0;
}
