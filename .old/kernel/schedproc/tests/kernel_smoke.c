/*
 * kernel_smoke.c
 *
 * Kernel-side smoke coverage for the standalone schedproc module.
 *
 * The compatibility layer already drives the real kernel scheduler. This test
 * keeps the smoke scope focused on the embeddable schedproc API itself by
 * creating an isolated context inside a kernel thread and exercising the core
 * state transitions there.
 */
#include "../include/sp_scheduler.h"
#include "../include/sp_wait.h"

#include "../../include/log.h"

 /* Append sp_dump_state fragments into one bounded stack buffer. */
static void schedproc_kernel_emit_text(void* context, const char* text) {
    char** cursor = (char**)context;

    if (!cursor || !*cursor || !text) {
        return;
    }

    while (*text != '\0') {
        **cursor = *text;
        (*cursor)++;
        text++;
    }
}

/* Keep failure reporting uniform across every scheduler operation. */
static int schedproc_kernel_expect_ok(SP_RESULT result, const char* step) {
    if (result != SP_OK) {
        log_fail("schedproc smoke: %s failed (result=%d)", step, (int)result);
        return -1;
    }

    return 0;
}

/* Emit one compact scheduler snapshot through the normal kernel log. */
static void schedproc_kernel_log_state(PSP_CONTEXT context, const char* label) {
    char buffer[768];
    char* cursor = buffer;

    memset(buffer, 0, sizeof(buffer));
    sp_dump_state(context, schedproc_kernel_emit_text, &cursor);
    *cursor = '\0';
    log_info("schedproc smoke: %s %s", label, buffer);
}

/* Exercise creation, run queue rotation, sleeping, blocking, and exit state. */
int schedproc_kernel_smoke_test_main(void) {
    PSP_CONTEXT context = NULL;
    PSP_PROCESS process = NULL;
    PSP_THREAD init_thread = NULL;
    PSP_THREAD io_thread = NULL;
    PSP_THREAD worker_thread = NULL;
    SP_THREADINFO info;
    SP_PROCESSINFO process_info;
    SP_RESULT wait_status;

    if (schedproc_kernel_expect_ok(sp_init(NULL, &context), "sp_init") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_create_process(context, "shell", &process), "sp_create_process") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_create_thread(context, process, "init", 2u, 3u, &init_thread), "sp_create_thread(init)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_create_thread(context, process, "io", 2u, 3u, &io_thread), "sp_create_thread(io)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_create_thread(context, process, "worker", 10u, 5u, &worker_thread), "sp_create_thread(worker)") != 0) {
        return -1;
    }

    if (schedproc_kernel_expect_ok(sp_resume_thread(context, init_thread), "sp_resume_thread(init)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_resume_thread(context, io_thread), "sp_resume_thread(io)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_resume_thread(context, worker_thread), "sp_resume_thread(worker)") != 0) {
        return -1;
    }

    if (schedproc_kernel_expect_ok(sp_make_thread_runnable(context, init_thread, true), "sp_make_thread_runnable(init)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_make_thread_runnable(context, io_thread, true), "sp_make_thread_runnable(io)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_make_thread_runnable(context, worker_thread, true), "sp_make_thread_runnable(worker)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_dispatch(context, NULL), "sp_dispatch(initial)") != 0) {
        return -1;
    }
    schedproc_kernel_log_state(context, "after initial dispatch:");

    if (schedproc_kernel_expect_ok(sp_tick(context, 3u), "sp_tick(3)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_sleep_current(context, 4u), "sp_sleep_current") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_block_current(context), "sp_block_current") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_unblock_thread(context, init_thread, true), "sp_unblock_thread(init)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_dispatch(context, NULL), "sp_dispatch(after unblock)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_tick(context, 4u), "sp_tick(4)") != 0) {
        return -1;
    }
    schedproc_kernel_log_state(context, "after sleep/block/unblock flow:");

    if (schedproc_kernel_expect_ok(sp_query_thread(io_thread, &info), "sp_query_thread(io)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_query_process(process, &process_info), "sp_query_process(shell)") != 0) {
        return -1;
    }

    log_info("schedproc smoke: thread=%s state=%s quantum_left=%u",
        info.name,
        sp_thread_state_name(info.state),
        (unsigned)info.quantumLeft);
    wait_status = sp_wait_feature_status();
    if (wait_status != SP_E_NOT_IMPLEMENTED) {
        log_fail("schedproc smoke: wait status changed unexpectedly (%d)", (int)wait_status);
        return -1;
    }
    log_info("schedproc smoke: process snapshot captured");

    if (schedproc_kernel_expect_ok(sp_begin_process_exit(context, process), "sp_begin_process_exit") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_query_process(process, &process_info), "sp_query_process(exit)") != 0) {
        return -1;
    }

    log_info("schedproc smoke: process exit state=%s", sp_process_state_name(process_info.state));

    if (schedproc_kernel_expect_ok(sp_destroy_thread(context, io_thread), "sp_destroy_thread(io)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_destroy_thread(context, init_thread), "sp_destroy_thread(init)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_destroy_thread(context, worker_thread), "sp_destroy_thread(worker)") != 0) {
        return -1;
    }
    if (schedproc_kernel_expect_ok(sp_destroy_process(context, process), "sp_destroy_process") != 0) {
        return -1;
    }

    if (schedproc_kernel_expect_ok(sp_shutdown(context), "sp_shutdown") != 0) {
        return -1;
    }

    return 0;
}