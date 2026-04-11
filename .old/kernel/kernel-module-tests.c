/*
 * kernel-module-tests.c
 *
 * Boot-time smoke harness for kernel-side module tests.
 *
 * The host-side smoke tests already validate the library behavior in a normal
 * userspace toolchain. This runner adds one kernel-thread execution path so we
 * also validate that the modules can execute safely inside the freestanding
 * kernel environment.
 */
#include "include/kernel_module_tests.h"

#include "include/log.h"
#include "include/task.h"

 /* Module-local kernel smoke entry points implemented beside each subsystem. */
int ipc_kernel_smoke_test_main(void);
int schedproc_kernel_smoke_test_main(void);
int loader_kernel_smoke_test_main(void);

/* Guard against accidental double-start during boot refactors. */
static Bool g_kernel_module_tests_started = false;

typedef int (*KERNEL_TEST_ENTRY_FN)(void);

typedef struct KernelTestCaseStruct {
    const char* name;
    KERNEL_TEST_ENTRY_FN entry;
} KernelTestCase;

/* Run one named suite and convert its result into a pass/fail count. */
static int kernel_module_tests_run_case(const KernelTestCase* test_case) {
    int result;

    if (!test_case || !test_case->entry) {
        return 1;
    }

    log_test("[%s] starting", test_case->name);
    result = test_case->entry();
    if (result != 0) {
        log_fail("[%s] failed (result=%d)", test_case->name, result);
        return 1;
    }

    log_test("[%s] passed", test_case->name);
    return 0;
}

/* Dedicated kernel thread body so test execution never hijacks init_task. */
static void kernel_module_tests_main(Pointer arg) {
    static const KernelTestCase test_cases[] = {
        { "ipc", ipc_kernel_smoke_test_main },
        { "schedproc", schedproc_kernel_smoke_test_main },
        { "loader", loader_kernel_smoke_test_main },
    };
    int failure_count = 0;

    (void)arg;
    log_info("Kernel module smoke runner started");

    for (UInt index = 0; index < (UInt)(sizeof(test_cases) / sizeof(test_cases[0])); ++index) {
        failure_count += kernel_module_tests_run_case(&test_cases[index]);
    }

    if (failure_count == 0) {
        log_info("Kernel module smoke runner finished successfully");
    }
    else {
        log_warning("Kernel module smoke runner finished with %d failure(s)", failure_count);
    }

    /*
     * Exit as a normal kernel thread so the scheduler reclaims the runner once
     * the suites have completed.
     */
    exit_current_process(failure_count == 0 ? 0 : -1);
}

void kernel_module_tests_start(void) {
    Process* process;
    Task* task;
    long pid;

    if (g_kernel_module_tests_started) {
        return;
    }

    pid = (long)process_create_main_thread(PF_KTHREAD, (Address)&kernel_module_tests_main, 0);
    if (pid < 0) {
        log_warning("Unable to start kernel module smoke runner");
        return;
    }

    task = process_main_thread(pid);
    process = process_lookup(pid);
    if (!task || !process) {
        log_warning("Kernel module smoke runner bookkeeping missing for pid %ld", pid);
        return;
    }

    /* Name both the process box and the thread so boot logs stay readable. */
    strncpy(process->name, "mod-tests", sizeof(process->name) - 1);
    process->name[sizeof(process->name) - 1] = '\0';
    strncpy(task->thread_name, "mod-tests", sizeof(task->thread_name) - 1);
    task->thread_name[sizeof(task->thread_name) - 1] = '\0';
    task->name = (Buffer)task->thread_name;
    task->priority = PRIORITY_NORMAL;
    g_kernel_module_tests_started = true;
}