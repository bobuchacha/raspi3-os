/*
 * ipc_debug.c
 *
 * File name:
 *   ipc_debug.c
 *
 * Purpose:
 *   Renders the current IPC registry state in a compact human-readable format.
 *
 * Design:
 *   The dump output is plain text so it can be used in smoke tests, debugger
 *   consoles, serial logs, or simple kernel tracing sinks with no extra tools.
 */
#include "../include/ipc_internal.h"

#include <stdarg.h>
#include <string.h>

#if !defined(__STDC_HOSTED__) || (__STDC_HOSTED__ == 0)
#include "../include/printf.h"
#endif

 /* Append one formatted character into a stack buffer. */
static void ipc_dump_putc(void* context, char ch) {
    char** cursor = (char**)context;

    if (!cursor || !*cursor) {
        return;
    }

    **cursor = ch;
    (*cursor)++;
}

/*
 * Centralize text emission so the same dump helper works in both:
 * - hosted smoke tests, where FILE streams and stdio are available, and
 * - freestanding kernel builds, where the serial console is the only sink.
 */
static void ipc_dump_vprint(FILE* stream, const char* format, va_list args) {
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
    vfprintf(stream, format, args);
#else
    char buffer[192];
    char* cursor = buffer;

    (void)stream;
    memset(buffer, 0, sizeof(buffer));
    tfp_format(&cursor, ipc_dump_putc, (char*)format, args);
    *cursor = '\0';
    kprint("%s", buffer);
#endif
}

/* Thin convenience wrapper around the varargs emission helper above. */
static void ipc_dump_print(FILE* stream, const char* format, ...) {
    va_list args;

    va_start(args, format);
    ipc_dump_vprint(stream, format, args);
    va_end(args);
}

void ipc_dump_state(PCIPC_CONTEXT context, FILE* stream) {
    PCIPC_EVENT   event;
    PCIPC_MAPPING mapping;
    PCIPC_MAILBOX mailbox;

    /*
     * Dump procedure:
     * - ignore NULL streams to keep the helper side-effect free
     * - print one section per object family
     * - linearly walk each registry and report the fields most useful during
     *   bring-up and smoke-test debugging
     */

    if (!stream) {
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
        return;
#endif
    }
    if (!context) {
        ipc_dump_print(stream, "ipc: <null context>\n");
        return;
    }

    ipc_dump_print(stream, "events:\n");
    for (event = context->eventHead; event; event = event->nextEvent) {
        ipc_dump_print(stream,
            "  %s manual=%u signaled=%u refs=%u sets=%u\n",
            event->name,
            event->manualReset ? 1u : 0u,
            event->signaled ? 1u : 0u,
            (unsigned)event->referenceCount,
            (unsigned)event->setCount);
    }

    ipc_dump_print(stream, "mappings:\n");
    for (mapping = context->mappingHead; mapping; mapping = mapping->nextMapping) {
        ipc_dump_print(stream,
            "  %s size=%u views=%u refs=%u seq=%u\n",
            mapping->name,
            (unsigned)mapping->size,
            (unsigned)mapping->activeViews,
            (unsigned)mapping->referenceCount,
            (unsigned)mapping->sequence);
    }

    ipc_dump_print(stream, "mailboxes:\n");
    for (mailbox = context->mailboxHead; mailbox; mailbox = mailbox->nextMailbox) {
        ipc_dump_print(stream,
            "  %s slot=%lu count=%lu queued=%lu high=%lu refs=%u\n",
            mailbox->name,
            (unsigned long)mailbox->slotSize,
            (unsigned long)mailbox->slotCount,
            (unsigned long)mailbox->messageCount,
            (unsigned long)mailbox->highWatermark,
            (unsigned)mailbox->referenceCount);
    }
}
