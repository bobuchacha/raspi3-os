/*
 * smoke.c
 *
 * Host-side smoke test for the scaffolded scheduler/process-manager module.
 */
#include "../include/sp_scheduler.h"
#include "../include/sp_wait.h"

#include <stdio.h>
#include <stdlib.h>

static void
require_ok(SP_RESULT result, const char* step) {
    if (result != SP_OK) {
        fprintf(stderr, "step failed: %s (result=%d)\n", step, (int)result);
        exit(1);
    }
}

static void
smoke_emit_text(void *context, const char *text) {
    FILE *stream = (FILE *)context;

    if (!stream || !text) {
        return;
    }

    fputs(text, stream);
}

int scheduler_process_man_test_main(void) {
    PSP_CONTEXT    context = NULL;
    PSP_PROCESS    process = NULL;
    PSP_THREAD     initThread = NULL;
    PSP_THREAD     ioThread = NULL;
    PSP_THREAD     workerThread = NULL;
    SP_THREADINFO  info;
    SP_PROCESSINFO procInfo;

    require_ok(sp_init(NULL, &context), "sp_init");
    require_ok(sp_create_process(context, "shell", &process), "sp_create_process");
    require_ok(sp_create_thread(context, process, "init", 2u, 3u, &initThread), "sp_create_thread(init)");
    require_ok(sp_create_thread(context, process, "io", 2u, 3u, &ioThread), "sp_create_thread(io)");
    require_ok(sp_create_thread(context, process, "worker", 10u, 5u, &workerThread), "sp_create_thread(worker)");

    require_ok(sp_resume_thread(context, initThread), "sp_resume_thread(init)");
    require_ok(sp_resume_thread(context, ioThread), "sp_resume_thread(io)");
    require_ok(sp_resume_thread(context, workerThread), "sp_resume_thread(worker)");

    require_ok(sp_make_thread_runnable(context, initThread, true), "sp_make_thread_runnable(init)");
    require_ok(sp_make_thread_runnable(context, ioThread, true), "sp_make_thread_runnable(io)");
    require_ok(sp_make_thread_runnable(context, workerThread, true), "sp_make_thread_runnable(worker)");
    require_ok(sp_dispatch(context, NULL), "sp_dispatch(initial)");

    puts("== initial ==");
    sp_dump_state(context, smoke_emit_text, stdout);

    require_ok(sp_tick(context, 3u), "sp_tick(3)");
    puts("== after 3 ticks, equal-priority rotation ==");
    sp_dump_state(context, smoke_emit_text, stdout);

    require_ok(sp_sleep_current(context, 4u), "sp_sleep_current(io)");
    puts("== after sleeping current ==");
    sp_dump_state(context, smoke_emit_text, stdout);

    require_ok(sp_block_current(context), "sp_block_current(init)");
    puts("== after blocking current ==");
    sp_dump_state(context, smoke_emit_text, stdout);

    require_ok(sp_unblock_thread(context, initThread, true), "sp_unblock_thread(init)");
    require_ok(sp_dispatch(context, NULL), "sp_dispatch(after unblock)");
    puts("== after unblocking init ==");
    sp_dump_state(context, smoke_emit_text, stdout);

    require_ok(sp_tick(context, 4u), "sp_tick(4)");
    puts("== after waking sleeper ==");
    sp_dump_state(context, smoke_emit_text, stdout);

    require_ok(sp_query_thread(ioThread, &info), "sp_query_thread(io)");
    require_ok(sp_query_process(process, &procInfo), "sp_query_process(shell)");
    printf("thread=%s state=%s quantum_left=%u\n", info.name, sp_thread_state_name(info.state), (unsigned)info.quantumLeft);
    printf("process=%s state=%s thread_count=%u\n", procInfo.name, sp_process_state_name(procInfo.state), (unsigned)procInfo.threadCount);
    printf("wait feature status=%d\n", (int)sp_wait_feature_status());

    require_ok(sp_begin_process_exit(context, process), "sp_begin_process_exit");
    require_ok(sp_query_process(process, &procInfo), "sp_query_process(exit)");
    printf("process exit state=%s\n", sp_process_state_name(procInfo.state));

    require_ok(sp_shutdown(context), "sp_shutdown");
    return 0;
}
