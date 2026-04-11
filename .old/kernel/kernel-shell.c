/*
 * kernel-shell.c
 *
 * Interactive serial shell for kernel diagnostics, task inspection, memory
 * tools, and bring-up commands.
 *
 * The shell is intentionally self-contained: formatting helpers, table layout,
 * argument parsing, and command handlers all live here so the shell remains
 * available even when higher-level user tools are not yet working.
 */
#include "arch/cortex-a53/mmu.h"
#include "device.h"
#if !defined(ROS_BOARD_VIRT)
#include "device/raspi3b/mailbox.h"
#endif
#include "filesystem/filesystem.h"
#include "filesystem/vfs/vfs.h"
#include "graphics.h"
#include "input.h"
#include "ldr_format.h"
#include "memory.h"
#include "hal/hal.h"
#include "module.h"
#include "percpu.h"
#include "printf.h"
#include "ros.h"
#include "task.h"
#include "timer.h"
#include "touch.h"
#include "utils.h"
#include "log.h"

#define SHELL_INPUT_MAX 128
#define SHELL_PATH_MAX 128
#define SHELL_NAME_MAX 32

#define SHELL_COLOR_RESET "\x1b[0m"
#define SHELL_COLOR_TITLE "\x1b[1;36m"
#define SHELL_COLOR_PROMPT "\x1b[1;32m"
#define SHELL_COLOR_LABEL "\x1b[1;34m"
#define SHELL_COLOR_MUTED "\x1b[2;37m"
#define SHELL_COLOR_OK "\x1b[32m"
#define SHELL_COLOR_WARN "\x1b[33m"
#define SHELL_COLOR_ERROR "\x1b[31m"
#define SHELL_COLOR_PATH "\x1b[36m"
#define SHELL_COLOR_VALUE "\x1b[97m"
#define SHELL_COLOR_PHYS SHELL_COLOR_OK
#define SHELL_COLOR_VIRT SHELL_COLOR_WARN

#define SHELL_HELP_COL_WIDTH 30
#define SHELL_PS_ID_WIDTH 6
#define SHELL_PS_STATE_WIDTH 12
#define SHELL_PS_COUNT_WIDTH 10
#define SHELL_PS_PRIORITY_WIDTH 10
#define SHELL_LS_TYPE_WIDTH 8
#define SHELL_LS_SIZE_WIDTH 12
#define SHELL_MEM_TRANSFER_MAX 4096UL
#define SHELL_WRITE_MAX_BYTES 64
#define SHELL_MONITOR_DEFAULT_SAMPLES 1UL
#define SHELL_MONITOR_DEFAULT_DELAY_MS 1000UL
#define SHELL_MONITOR_MAX_SAMPLES 120UL
#define SHELL_MONITOR_MAX_DELAY_MS 10000UL
#define SHELL_HEAPTEST_BLOCK_COUNT 8
#define SHELL_HEAPTEST_MAX_LOOPS 128UL

static const char shell_banner[] =
"\r\n"
SHELL_COLOR_TITLE "ROS serial shell" SHELL_COLOR_RESET "\r\n"
SHELL_COLOR_MUTED "Type 'help' for commands." SHELL_COLOR_RESET "\r\n\r\n";

static const char shell_prompt[] = SHELL_COLOR_PROMPT "ros> " SHELL_COLOR_RESET;

static int shell_parse_ulong(const char* text, unsigned long* value);
static char* shell_next_token(char** cursor);

/* Keep serial shell input on UART only; do not mirror it into the framebuffer GUI. */
static void shell_forward_gui_key(char ch) {
    (void)ch;
}

/* Tiny tfp_format sink that appends one character into a moving buffer cursor. */
static void shell_putcp(void* cursorContext, char ch) {
    *(*((char**)cursorContext))++ = ch;
}

/* Format one shell message through tfp_format and print the final buffer. */
static void shell_vprint(const char* fmt, va_list args) {
    char buffer[256];
    char* cursor = buffer;

    tfp_format(&cursor, shell_putcp, (char*)fmt, args);
    shell_putcp(&cursor, 0);
    kprint("%s", buffer);
}

/* Print the standard shell separator line used by tables and headings. */
static void shell_print_separator(void) {
    kprint(SHELL_COLOR_MUTED "-------------------------------------------------------------------------------" SHELL_COLOR_RESET "\r\n");
}

/* Compute the visible length of one NUL-terminated shell string. */
static int shell_text_len(const char* text) {
    int len = 0;

    if (!text) {
        return 0;
    }
    while (text[len] != '\0') {
        len++;
    }

    return len;
}

/* Emit a requested number of ASCII spaces. */
static void shell_print_spaces(int count) {
    while (count-- > 0) {
        kprint(" ");
    }
}

/* Print one text cell with optional color and fixed-width padding. */
static void shell_print_text_column(const char* text, int width, const char* color) {
    int len = shell_text_len(text);

    if (!text) {
        text = "";
        len = 0;
    }

    if (color) {
        kprint("%s", (char*)color);
    }
    kprint("%s", (char*)text);
    if (color) {
        kprint(SHELL_COLOR_RESET);
    }
    if (len < width) {
        shell_print_spaces(width - len);
    }
}

/* Format one string directly into a caller-provided stack buffer. */
static void shell_format_text(char* buffer, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    tfp_format(&buffer, shell_putcp, (char*)fmt, args);
    shell_putcp(&buffer, 0);
    va_end(args);
}

/* Print one titled shell section heading. */
static void shell_print_heading(const char* title) {
    shell_print_separator();
    kprint(SHELL_COLOR_TITLE "%s" SHELL_COLOR_RESET "\r\n", title);
    shell_print_separator();
}

/* Print one label/value pair using the shell's table styling. */
static void shell_print_kv(const char* label, const char* fmt, ...) {
    va_list args;

    kprint("  ");
    shell_print_text_column(label, 14, SHELL_COLOR_LABEL);
    kprint(" " SHELL_COLOR_MUTED "|" SHELL_COLOR_RESET " " SHELL_COLOR_VALUE);
    va_start(args, fmt);
    shell_vprint(fmt, args);
    va_end(args);
    kprint(SHELL_COLOR_RESET "\r\n");
}

/* Clear the serial terminal using ANSI escape sequences. */
static void shell_clear_screen(void) {
    kprint("\033[2J\033[H");
}

/* Print one shell error line with consistent coloring and prefixing. */
static void shell_print_error(const char* fmt, ...) {
    va_list args;

    kprint(SHELL_COLOR_ERROR "error: " SHELL_COLOR_RESET);
    va_start(args, fmt);
    shell_vprint(fmt, args);
    va_end(args);
    kprint("\r\n");
}

/* Print one shell usage line for malformed command input. */
static void shell_print_usage(const char* fmt, ...) {
    va_list args;

    kprint(SHELL_COLOR_WARN "usage: " SHELL_COLOR_RESET);
    va_start(args, fmt);
    shell_vprint(fmt, args);
    va_end(args);
    kprint("\r\n");
}

/* Map task states to the shell color palette used by task listings. */
static const char* shell_task_state_color(const char* state) {
    if (strcmp(state, "RUN") == 0) {
        return SHELL_COLOR_OK;
    }
    if (strcmp(state, "READY") == 0) {
        return SHELL_COLOR_PATH;
    }
    if (strcmp(state, "SLEEP") == 0) {
        return SHELL_COLOR_WARN;
    }
    if (strcmp(state, "BLOCK") == 0) {
        return SHELL_COLOR_PATH;
    }
    return SHELL_COLOR_ERROR;
}

/* Pick the most useful human-readable task name for shell output. */
static const char* shell_task_display_name(Task* task) {
    const char* name;

    if (!task) {
        return "<none>";
    }

    name = task->name ? (char*)task->name : "";
    if (name[0] == '\0' && task->process && task->process->name[0] != '\0') {
        name = task->process->name;
    }
    if (name[0] == '\0') {
        name = "<unnamed>";
    }

    return name;
}

/* Convert one TaskState enum value into the shell's short status text. */
static const char* shell_task_state_text(TaskState state) {
    switch (state) {
    case TASK_READY:
        return "READY";
    case TASK_RUNNING:
        return "RUN";
    case TASK_SLEEPING:
        return "SLEEP";
    case TASK_BLOCKED:
        return "BLOCK";
    case TASK_ZOMBIE:
    default:
        return "ZOMBIE";
    }
}

/* Read MPIDR_EL1 for CPU affinity and topology diagnostics. */
static unsigned long shell_read_mpidr_el1(void) {
    unsigned long value;

    asm volatile("mrs %0, mpidr_el1" : "=r"(value));
    return value;
}

/* Read MIDR_EL1 so the shell can display CPU implementer and part number. */
static unsigned long shell_read_midr_el1(void) {
    unsigned long value;

    asm volatile("mrs %0, midr_el1" : "=r"(value));
    return value;
}

/* Read CNTFRQ_EL0 to show the architected timer base frequency. */
static unsigned long shell_read_cntfrq_el0(void) {
    unsigned long value;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

/* Count CPUs currently marked online by the percpu layer. */
static unsigned int shell_count_online_cpus(void) {
    unsigned int online = 0;
    unsigned int cpu_index;

    for (cpu_index = 0; cpu_index < NR_CPUS; cpu_index++) {
        if (percpu_cpu_is_online(cpu_index)) {
            online++;
        }
    }

    return online;
}

/* Count all tasks whose affinity points at one CPU slot. */
static unsigned int shell_count_tasks_for_cpu(unsigned int cpu_index) {
    unsigned int count = 0;
    int task_index;

    for (task_index = 0; task_index < NR_TASKS; task_index++) {
        Task* task = tasks[task_index];

        if (task && task->cpu_affinity == cpu_index) {
            count++;
        }
    }

    return count;
}

/* Count all tasks for one CPU that are currently in the requested state. */
static unsigned int shell_count_tasks_for_cpu_in_state(unsigned int cpu_index, TaskState state) {
    unsigned int count = 0;
    int task_index;

    for (task_index = 0; task_index < NR_TASKS; task_index++) {
        Task* task = tasks[task_index];

        if (task && task->cpu_affinity == cpu_index && task->state == state) {
            count++;
        }
    }

    return count;
}

/* Count all tasks globally that match one scheduler-visible state. */
static unsigned int shell_count_all_tasks_in_state(TaskState state) {
    unsigned int count = 0;
    int task_index;

    for (task_index = 0; task_index < NR_TASKS; task_index++) {
        if (tasks[task_index] && tasks[task_index]->state == state) {
            count++;
        }
    }

    return count;
}

/* Count online CPUs other than the boot CPU. */
static unsigned int shell_count_online_secondary_cpus(void) {
    unsigned int online = shell_count_online_cpus();

    if (online == 0) {
        return 0;
    }
    return online - 1;
}

/* Count CPUs currently doing non-idle work. */
static unsigned int shell_count_busy_cpus(void) {
    unsigned int busy = 0;
    unsigned int cpu_index;

    for (cpu_index = 0; cpu_index < NR_CPUS; cpu_index++) {
        Task* current;

        if (!percpu_cpu_is_online(cpu_index)) {
            continue;
        }

        current = current_tasks[cpu_index];
        if (!current) {
            continue;
        }
        if (cpu_index != 0 && current == percpu_idle_task(cpu_index)) {
            continue;
        }

        busy++;
    }

    return busy;
}

/* Format one used/total ratio as a fixed single-decimal percentage string. */
static void shell_format_percent(char* buffer, unsigned long used, unsigned long total) {
    unsigned long scaled;

    if (total == 0) {
        strncpy(buffer, "n/a", 15);
        buffer[15] = '\0';
        return;
    }

    scaled = (used * 1000UL) / total;
    shell_format_text(buffer, "%lu.%lu%%", scaled / 10UL, scaled % 10UL);
}

/* Delay between monitor samples without duplicating the zero-delay check. */
static void shell_monitor_wait(unsigned long delay_ms) {
    if (delay_ms == 0) {
        return;
    }

    wait_msec((unsigned int)delay_ms);
}

/* Parse the common monitor command arguments shared by stats subcommands. */
static int shell_parse_monitor_args(char** cursor, const char* usage, unsigned long* samples, unsigned long* delay_ms) {
    char* samples_text = shell_next_token(cursor);
    char* delay_text = shell_next_token(cursor);

    *samples = SHELL_MONITOR_DEFAULT_SAMPLES;
    *delay_ms = SHELL_MONITOR_DEFAULT_DELAY_MS;

    if (samples_text && shell_parse_ulong(samples_text, samples) != 0) {
        shell_print_usage("%s", usage);
        return -1;
    }
    if (delay_text && shell_parse_ulong(delay_text, delay_ms) != 0) {
        shell_print_usage("%s", usage);
        return -1;
    }
    if (*samples == 0 || *samples > SHELL_MONITOR_MAX_SAMPLES) {
        shell_print_error("Sample count must be between 1 and %lu", SHELL_MONITOR_MAX_SAMPLES);
        return -1;
    }
    if (*delay_ms > SHELL_MONITOR_MAX_DELAY_MS) {
        shell_print_error("Delay must be between 0 and %lu ms", SHELL_MONITOR_MAX_DELAY_MS);
        return -1;
    }

    return 0;
}

/* Print the standard heading block for one live monitor sample. */
static void shell_print_monitor_stamp(const char* title, unsigned long sample_index, unsigned long samples, unsigned long delay_ms) {
    shell_print_heading(title);
    shell_print_kv("sample", "%lu / %lu", sample_index + 1, samples);
    shell_print_kv("delay", "%lu ms", delay_ms);
    shell_print_kv("ticks", "%lu", schedler_get_ticks());
    shell_print_kv("timer", "%lu us", get_system_timer());
}

static const char* shell_cpu_implementer_name(unsigned int implementer) {
    if (implementer == 0x41U) {
        return "Arm";
    }
    return "unknown";
}

static const char* shell_cpu_part_name(unsigned int part) {
    if (part == 0xD03U) {
        return "Cortex-A53";
    }
    return "unknown";
}

static void shell_show_cpu_info(void) {
    unsigned long mpidr = shell_read_mpidr_el1();
    unsigned long midr = shell_read_midr_el1();
    unsigned int current_cpu = task_cpu_index();
    unsigned int online_cpus = shell_count_online_cpus();
    unsigned int implementer = (unsigned int)((midr >> 24) & 0xFFUL);
    unsigned int variant = (unsigned int)((midr >> 20) & 0xFUL);
    unsigned int architecture = (unsigned int)((midr >> 16) & 0xFUL);
    unsigned int part = (unsigned int)((midr >> 4) & 0xFFFUL);
    unsigned int revision = (unsigned int)(midr & 0xFUL);
    char number[32];
    unsigned int cpu_index;

    shell_print_heading("CPU Overview");
    shell_print_kv("current cpu", "%u", current_cpu);
    shell_print_kv("online", "%u / %u", online_cpus, (unsigned int)NR_CPUS);
    shell_print_kv("mpidr_el1", "0x%lX", mpidr);
    shell_print_kv("midr_el1", "0x%lX", midr);
    shell_print_kv("implementer", "%s (0x%X)", shell_cpu_implementer_name(implementer), implementer);
    shell_print_kv("part", "%s (0x%X)", shell_cpu_part_name(part), part);
    shell_print_kv("variant", "%u", variant);
    shell_print_kv("revision", "%u", revision);
    shell_print_kv("arch field", "0x%X", architecture);

    shell_print_heading("Per-CPU State");
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("CPU", 4, null);
    kprint(" ");
    shell_print_text_column("ONLINE", 8, null);
    kprint(" ");
    shell_print_text_column("CURRENT", 8, null);
    kprint(" ");
    shell_print_text_column("BOUND", 6, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (cpu_index = 0; cpu_index < NR_CPUS; cpu_index++) {
        Task* task = current_tasks[cpu_index];

        kprint("  ");
        shell_format_text(number, "%u", cpu_index);
        shell_print_text_column(number, 4, null);
        kprint(" ");
        shell_print_text_column(percpu_cpu_is_online(cpu_index) ? "yes" : "no", 8,
            percpu_cpu_is_online(cpu_index) ? SHELL_COLOR_OK : SHELL_COLOR_WARN);
        kprint(" ");
        if (task) {
            shell_format_text(number, "%ld", task->id);
            shell_print_text_column(number, 8, null);
        }
        else {
            shell_print_text_column("-", 8, null);
        }
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu(cpu_index));
        shell_print_text_column(number, 6, null);
        kprint(" %s\r\n", shell_task_display_name(task));
    }
}

static void shell_show_cpu_detail(const char* cpu_text) {
    unsigned long mpidr = shell_read_mpidr_el1();
    unsigned int current_cpu = task_cpu_index();
    unsigned long cpu_index_value;
    unsigned int cpu_index;
    Task* current;
    Task* idle;

    if (!cpu_text || shell_parse_ulong(cpu_text, &cpu_index_value) != 0 || cpu_index_value >= (unsigned long)NR_CPUS) {
        shell_print_usage("cpu [id]");
        return;
    }

    cpu_index = (unsigned int)cpu_index_value;
    current = current_tasks[cpu_index];
    idle = cpu_index == 0 ? 0 : percpu_idle_task(cpu_index);

    shell_print_heading("CPU Detail");
    shell_print_kv("cpu", "%u", cpu_index);
    shell_print_kv("online", "%s", percpu_cpu_is_online(cpu_index) ? "yes" : "no");
    shell_print_kv("bound tasks", "%u", shell_count_tasks_for_cpu(cpu_index));
    shell_print_kv("running", "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_RUNNING));
    shell_print_kv("sleeping", "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_SLEEPING));
    shell_print_kv("zombies", "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_ZOMBIE));

    if (current) {
        shell_print_kv("current id", "%ld", current->id);
        shell_print_kv("current name", "%s", shell_task_display_name(current));
        shell_print_kv("current state", "%s", shell_task_state_text(current->state));
        shell_print_kv("timeslice", "%ld", current->counter);
        shell_print_kv("priority", "%ld", current->priority);
        shell_print_kv("preempt", "%ld", current->preempt_count);
        shell_print_kv("pgd", "0x%lX", current->mm.pgd);
    }
    else {
        shell_print_kv("current", "%s", "<none>");
    }

    if (idle) {
        shell_print_kv("idle task", "%ld (%s)", idle->id, shell_task_display_name(idle));
        shell_print_kv("idle active", "%s", current == idle ? "yes" : "no");
        shell_print_kv("stack top", "0x%lX", percpu_stack_top(cpu_index));
    }
    else if (cpu_index == 0) {
        shell_print_kv("idle task", "%s", "boot CPU uses init task path");
    }

    if (cpu_index == current_cpu) {
        shell_print_kv("mpidr_el1", "0x%lX", mpidr);
    }
    else {
        shell_print_kv("mpidr_el1", "%s", "only readable for the current CPU");
    }
}

static void shell_show_scheduler_info(void) {
    char number[32];
    unsigned int cpu_index;

    shell_print_heading("Scheduler Info");
    shell_print_kv("ticks", "%lu", schedler_get_ticks());
    shell_print_kv("tasks total", "%d / %d", nr_tasks, NR_TASKS);
    shell_print_kv("running", "%u", shell_count_all_tasks_in_state(TASK_RUNNING));
    shell_print_kv("sleeping", "%u", shell_count_all_tasks_in_state(TASK_SLEEPING));
    shell_print_kv("zombies", "%u", shell_count_all_tasks_in_state(TASK_ZOMBIE));
    shell_print_kv("online cpus", "%u", shell_count_online_cpus());
    shell_print_kv("secondary cpus", "%u", shell_count_online_secondary_cpus());

    shell_print_heading("Per-CPU Scheduler Counters");
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("CPU", 4, null);
    kprint(" ");
    shell_print_text_column("RUN", 6, null);
    kprint(" ");
    shell_print_text_column("SLEEP", 8, null);
    kprint(" ");
    shell_print_text_column("ZOMB", 6, null);
    kprint(" ");
    shell_print_text_column("CURR", 6, null);
    kprint(" ");
    shell_print_text_column("IDLE", 6, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (cpu_index = 0; cpu_index < NR_CPUS; cpu_index++) {
        Task* current = current_tasks[cpu_index];
        Task* idle = cpu_index == 0 ? 0 : percpu_idle_task(cpu_index);

        kprint("  ");
        shell_format_text(number, "%u", cpu_index);
        shell_print_text_column(number, 4, null);
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_RUNNING));
        shell_print_text_column(number, 6, null);
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_SLEEPING));
        shell_print_text_column(number, 8, null);
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_ZOMBIE));
        shell_print_text_column(number, 6, null);
        kprint(" ");
        if (current) {
            shell_format_text(number, "%ld", current->id);
            shell_print_text_column(number, 6, null);
        }
        else {
            shell_print_text_column("-", 6, null);
        }
        kprint(" ");
        shell_print_text_column(current && idle && current == idle ? "yes" : "no", 6,
            current && idle && current == idle ? SHELL_COLOR_WARN : SHELL_COLOR_OK);
        kprint(" %s\r\n", shell_task_display_name(current));
    }
}

static void shell_show_top_snapshot(unsigned long sample_index, unsigned long samples, unsigned long delay_ms) {
    char number[32];
    int index;

    shell_print_monitor_stamp("Top", sample_index, samples, delay_ms);
    shell_print_kv("running", "%u", shell_count_all_tasks_in_state(TASK_RUNNING));
    shell_print_kv("sleeping", "%u", shell_count_all_tasks_in_state(TASK_SLEEPING));
    shell_print_kv("zombies", "%u", shell_count_all_tasks_in_state(TASK_ZOMBIE));

    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("PID", SHELL_PS_ID_WIDTH, null);
    kprint(" ");
    shell_print_text_column("CPU", 4, null);
    kprint(" ");
    shell_print_text_column("STATE", SHELL_PS_STATE_WIDTH, null);
    kprint(" ");
    shell_print_text_column("COUNT", SHELL_PS_COUNT_WIDTH, null);
    kprint(" ");
    shell_print_text_column("PRIO", 6, null);
    kprint(" ");
    shell_print_text_column("PREEMPT", 8, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (index = 0; index < NR_TASKS; index++) {
        Task* task = tasks[index];
        const char* state;

        if (!task) {
            continue;
        }

        state = shell_task_state_text(task->state);

        kprint("  ");
        shell_format_text(number, "%ld", task->id);
        shell_print_text_column(number, SHELL_PS_ID_WIDTH, null);
        kprint(" ");
        shell_format_text(number, "%u", task->cpu_affinity);
        shell_print_text_column(number, 4, null);
        kprint(" ");
        shell_print_text_column(state, SHELL_PS_STATE_WIDTH, shell_task_state_color(state));
        kprint(" ");
        shell_format_text(number, "%ld", task->counter);
        shell_print_text_column(number, SHELL_PS_COUNT_WIDTH, null);
        kprint(" ");
        shell_format_text(number, "%ld", task->priority);
        shell_print_text_column(number, 6, null);
        kprint(" ");
        shell_format_text(number, "%ld", task->preempt_count);
        shell_print_text_column(number, 8, null);
        kprint(" %s\r\n", shell_task_display_name(task));
    }
}

static void shell_show_mpstat_snapshot(unsigned long sample_index, unsigned long samples, unsigned long delay_ms) {
    char number[32];
    unsigned int cpu_index;

    shell_print_monitor_stamp("Mpstat", sample_index, samples, delay_ms);
    shell_print_kv("online cpus", "%u", shell_count_online_cpus());
    shell_print_kv("secondary cpus", "%u", shell_count_online_secondary_cpus());

    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("CPU", 4, null);
    kprint(" ");
    shell_print_text_column("ON", 4, null);
    kprint(" ");
    shell_print_text_column("RUN", 6, null);
    kprint(" ");
    shell_print_text_column("SLEEP", 8, null);
    kprint(" ");
    shell_print_text_column("ZOMB", 6, null);
    kprint(" ");
    shell_print_text_column("CURR", 6, null);
    kprint(" ");
    shell_print_text_column("IDLE", 6, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (cpu_index = 0; cpu_index < NR_CPUS; cpu_index++) {
        Task* current = current_tasks[cpu_index];
        Task* idle = cpu_index == 0 ? 0 : percpu_idle_task(cpu_index);
        Bool online = percpu_cpu_is_online(cpu_index);
        Bool idle_active = current && idle && current == idle;

        kprint("  ");
        shell_format_text(number, "%u", cpu_index);
        shell_print_text_column(number, 4, null);
        kprint(" ");
        shell_print_text_column(online ? "yes" : "no", 4, online ? SHELL_COLOR_OK : SHELL_COLOR_WARN);
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_RUNNING));
        shell_print_text_column(number, 6, null);
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_SLEEPING));
        shell_print_text_column(number, 8, null);
        kprint(" ");
        shell_format_text(number, "%u", shell_count_tasks_for_cpu_in_state(cpu_index, TASK_ZOMBIE));
        shell_print_text_column(number, 6, null);
        kprint(" ");
        if (current) {
            shell_format_text(number, "%ld", current->id);
            shell_print_text_column(number, 6, null);
        }
        else {
            shell_print_text_column("-", 6, null);
        }
        kprint(" ");
        shell_print_text_column(idle_active ? "yes" : "no", 6, idle_active ? SHELL_COLOR_WARN : SHELL_COLOR_OK);
        kprint(" %s\r\n", shell_task_display_name(current));
    }
}

static void shell_show_usage_snapshot(unsigned long sample_index, unsigned long samples, unsigned long delay_ms) {
    unsigned int online_cpus = shell_count_online_cpus();
    unsigned int busy_cpus = shell_count_busy_cpus();
    unsigned long total_pages = mem_get_size() / PAGE_SIZE;
    unsigned long free_pages = mem_get_free_pages();
    unsigned long used_pages = total_pages >= free_pages ? total_pages - free_pages : 0;
    unsigned long total_bytes = mem_get_size();
    unsigned long used_bytes = used_pages * PAGE_SIZE;
    unsigned long free_bytes = free_pages * PAGE_SIZE;
    unsigned long heap_total = mem_heap_total_bytes();
    unsigned long heap_used = mem_heap_used_bytes();
    unsigned long heap_free = mem_heap_free_bytes();
    char percent[16];

    shell_print_monitor_stamp("Usage", sample_index, samples, delay_ms);

    shell_format_percent(percent, busy_cpus, online_cpus);
    shell_print_kv("cpu busy", "%u / %u (%s)", busy_cpus, online_cpus, percent);
    shell_print_kv("running", "%u tasks", shell_count_all_tasks_in_state(TASK_RUNNING));
    shell_print_kv("sleeping", "%u tasks", shell_count_all_tasks_in_state(TASK_SLEEPING));

    shell_format_percent(percent, used_bytes, total_bytes);
    shell_print_kv("phys used", "%lu / %lu bytes (%s)", used_bytes, total_bytes, percent);
    shell_print_kv("phys free", "%lu bytes (%lu pages)", free_bytes, free_pages);

    shell_format_percent(percent, heap_used, heap_total);
    shell_print_kv("heap used", "%lu / %lu bytes (%s)", heap_used, heap_total, percent);
    shell_print_kv("heap free", "%lu bytes", heap_free);

    shell_print_heading("Per-CPU Busy Snapshot");
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("CPU", 4, null);
    kprint(" ");
    shell_print_text_column("STATE", 8, null);
    kprint(" ");
    shell_print_text_column("TASK", 6, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (unsigned int cpu_index = 0; cpu_index < NR_CPUS; cpu_index++) {
        Task* current = current_tasks[cpu_index];
        Bool online = percpu_cpu_is_online(cpu_index);
        Bool idle = cpu_index != 0 && current && current == percpu_idle_task(cpu_index);
        const char* state = !online ? "offline" : (idle ? "idle" : "busy");

        kprint("  ");
        shell_format_text(percent, "%u", cpu_index);
        shell_print_text_column(percent, 4, null);
        kprint(" ");
        shell_print_text_column(state, 8,
            !online ? SHELL_COLOR_WARN : (idle ? SHELL_COLOR_WARN : SHELL_COLOR_OK));
        kprint(" ");
        if (current) {
            shell_format_text(percent, "%ld", current->id);
            shell_print_text_column(percent, 6, null);
        }
        else {
            shell_print_text_column("-", 6, null);
        }
        kprint(" %s\r\n", shell_task_display_name(current));
    }
}

/*
 * Fill one heap block with a deterministic byte pattern so later validation can
 * detect overwrite, stale data, or realloc copy bugs.
 */
static void shell_heaptest_fill(Address ptr, unsigned long length, UByte seed) {
    UByte* bytes = (UByte*)(Pointer)ptr;
    unsigned long index;

    if (!ptr) {
        return;
    }

    // Write a repeatable pattern across the full payload range.
    for (index = 0; index < length; index++) {
        bytes[index] = (UByte)(seed + (UByte)index);
    }
}

/*
 * Verify that one heap block still contains the deterministic pattern written by
 * shell_heaptest_fill.
 */
static int shell_heaptest_verify(Address ptr, unsigned long length, UByte seed, unsigned long* mismatch_index) {
    UByte* bytes = (UByte*)(Pointer)ptr;
    unsigned long index;

    if (!ptr) {
        if (mismatch_index) {
            *mismatch_index = 0;
        }
        return -1;
    }

    // Scan the payload and report the first mismatched byte to aid debugging.
    for (index = 0; index < length; index++) {
        if (bytes[index] != (UByte)(seed + (UByte)index)) {
            if (mismatch_index) {
                *mismatch_index = index;
            }
            return -1;
        }
    }

    return 0;
}

/*
 * Release every temporary heap block used by the shell self-test and clear the
 * tracking table so cleanup can be called more than once safely.
 */
static void shell_heaptest_cleanup(Address* blocks, int count) {
    int index;

    if (!blocks || count <= 0) {
        return;
    }

    // Free any surviving allocations from the current test iteration.
    for (index = 0; index < count; index++) {
        if (blocks[index] != 0) {
            kfree(blocks[index]);                                // release the temporary test block
            blocks[index] = 0;
        }
    }
}

/*
 * Run one deterministic heap exercise that covers allocate, fragment, realloc,
 * verify, and full cleanup.
 */
static int shell_heaptest_once(unsigned long iteration, char* failure_reason, int failure_reason_size) {
    static const unsigned long initial_sizes[SHELL_HEAPTEST_BLOCK_COUNT] = { 24UL, 48UL, 96UL, 160UL, 320UL, 640UL, 1024UL, 2048UL };
    static const unsigned long refill_sizes[SHELL_HEAPTEST_BLOCK_COUNT] = { 0UL, 32UL, 0UL, 128UL, 0UL, 384UL, 0UL, 1536UL };
    Address blocks[SHELL_HEAPTEST_BLOCK_COUNT];
    unsigned long logical_sizes[SHELL_HEAPTEST_BLOCK_COUNT];
    unsigned long before_used = mem_heap_used_bytes();
    unsigned long before_free = mem_heap_free_bytes();
    unsigned long mismatch_index = 0;
    unsigned long after_used;
    unsigned long after_free;
    int index;

    memzero((Address)blocks, sizeof(blocks));                    // start with a clean tracking table
    memzero((Address)logical_sizes, sizeof(logical_sizes));      // clear logical payload sizes for verification

    // Allocate one mixed-size working set and seed each block with known data.
    for (index = 0; index < SHELL_HEAPTEST_BLOCK_COUNT; index++) {
        blocks[index] = kmalloc((int)initial_sizes[index]);      // allocate the initial working set
        if (blocks[index] == 0) {
            shell_format_text(failure_reason, "alloc failed at slot %d (%lu bytes)", index, initial_sizes[index]);
            shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT); // tear down the partial iteration state
            return -1;
        }
        logical_sizes[index] = initial_sizes[index];
        shell_heaptest_fill(blocks[index], logical_sizes[index], (UByte)(0x20U + index)); // seed the block with a predictable pattern
    }

    // Free every other block so later allocations have to reuse fragmented holes.
    for (index = 1; index < SHELL_HEAPTEST_BLOCK_COUNT; index += 2) {
        kfree(blocks[index]);                                    // create fragmentation between live allocations
        blocks[index] = 0;
        logical_sizes[index] = 0;
    }

    // Verify the surviving allocations were not damaged by neighbour frees.
    for (index = 0; index < SHELL_HEAPTEST_BLOCK_COUNT; index += 2) {
        if (shell_heaptest_verify(blocks[index], logical_sizes[index], (UByte)(0x20U + index), &mismatch_index) != 0) {
            shell_format_text(failure_reason, "verify failed at slot %d offset %lu after fragmentation", index, mismatch_index);
            shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT); // release all tracked allocations before returning
            return -1;
        }
    }

    // Refill the freed holes with different sizes to exercise splitting decisions.
    for (index = 1; index < SHELL_HEAPTEST_BLOCK_COUNT; index += 2) {
        blocks[index] = kmalloc((int)refill_sizes[index]);       // allocate into the fragmented free space
        if (blocks[index] == 0) {
            shell_format_text(failure_reason, "refill failed at slot %d (%lu bytes)", index, refill_sizes[index]);
            shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT); // release all tracked allocations before returning
            return -1;
        }
        logical_sizes[index] = refill_sizes[index];
        shell_heaptest_fill(blocks[index], logical_sizes[index], (UByte)(0x60U + index)); // seed the replacement blocks with a new pattern
    }

    // Grow one live block and make sure its original payload survives the realloc.
    blocks[4] = krealloc(blocks[4], 768U);                       // force a grow path through the allocator
    if (blocks[4] == 0) {
        shell_format_text(failure_reason, "realloc failed at slot 4");
        shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT); // release all tracked allocations before returning
        return -1;
    }
    if (shell_heaptest_verify(blocks[4], initial_sizes[4], (UByte)(0x20U + 4), &mismatch_index) != 0) {
        shell_format_text(failure_reason, "realloc verify failed at slot 4 offset %lu", mismatch_index);
        shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT); // release all tracked allocations before returning
        return -1;
    }
    logical_sizes[4] = 768U;
    shell_heaptest_fill(blocks[4], logical_sizes[4], (UByte)(0x90U + 4)); // refresh the grown block with a new full-size pattern

    // Re-verify the full working set before cleanup to catch cross-block corruption.
    for (index = 0; index < SHELL_HEAPTEST_BLOCK_COUNT; index++) {
        UByte seed = (index == 4) ? (UByte)(0x90U + index) : (index % 2 == 0 ? (UByte)(0x20U + index) : (UByte)(0x60U + index));

        if (shell_heaptest_verify(blocks[index], logical_sizes[index], seed, &mismatch_index) != 0) {
            shell_format_text(failure_reason, "final verify failed at slot %d offset %lu", index, mismatch_index);
            shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT); // release all tracked allocations before returning
            return -1;
        }
    }

    shell_heaptest_cleanup(blocks, SHELL_HEAPTEST_BLOCK_COUNT);  // return the heap to its pre-test state

    after_used = mem_heap_used_bytes();
    after_free = mem_heap_free_bytes();
    if (after_used != before_used || after_free != before_free) {
        shell_format_text(failure_reason,
            "heap accounting changed on iteration %lu (used %lu->%lu free %lu->%lu)",
            iteration + 1,
            before_used,
            after_used,
            before_free,
            after_free);
        return -1;
    }

    if (failure_reason_size > 0) {
        failure_reason[0] = '\0';
    }
    return 0;
}

/*
 * Run the shell-visible heap self-test command for one or more iterations and
 * report allocator health before and after the exercise.
 */
static void shell_run_heaptest(char* loops_text) {
    unsigned long loops = 1;
    unsigned long before_used = mem_heap_used_bytes();
    unsigned long before_free = mem_heap_free_bytes();
    unsigned long after_used;
    unsigned long after_free;
    char failure_reason[128];
    unsigned long iteration;

    if (loops_text && shell_parse_ulong(loops_text, &loops) != 0) {
        shell_print_usage("heaptest [loops]");
        return;
    }
    if (loops == 0 || loops > SHELL_HEAPTEST_MAX_LOOPS) {
        shell_print_error("Loop count must be between 1 and %lu", SHELL_HEAPTEST_MAX_LOOPS);
        return;
    }

    shell_print_heading("Heap Self-Test");
    shell_print_kv("loops", "%lu", loops);
    shell_print_kv("heap used", "%lu bytes", before_used);
    shell_print_kv("heap free", "%lu bytes", before_free);

    // Repeat the deterministic allocator exercise enough times to catch list corruption.
    for (iteration = 0; iteration < loops; iteration++) {
        if (shell_heaptest_once(iteration, failure_reason, sizeof(failure_reason)) != 0) {
            shell_print_kv("result", SHELL_COLOR_ERROR "FAIL" SHELL_COLOR_RESET);
            shell_print_kv("iteration", "%lu", iteration + 1);
            shell_print_kv("reason", "%s", failure_reason);
            mem_heap_dump(24);                                   // dump a short heap snapshot to help diagnose the failure
            return;
        }
    }

    after_used = mem_heap_used_bytes();
    after_free = mem_heap_free_bytes();
    shell_print_kv("result", SHELL_COLOR_OK "PASS" SHELL_COLOR_RESET);
    shell_print_kv("heap used", "%lu bytes", after_used);
    shell_print_kv("heap free", "%lu bytes", after_free);
}

static void shell_run_monitor(void (*snapshot)(unsigned long, unsigned long, unsigned long), unsigned long samples, unsigned long delay_ms) {
    unsigned long sample_index;

    for (sample_index = 0; sample_index < samples; sample_index++) {
        if (samples > 1) {
            shell_clear_screen();
        }
        snapshot(sample_index, samples, delay_ms);
        if (sample_index + 1 < samples) {
            shell_monitor_wait(delay_ms);
        }
    }
}

#if defined(ROS_BOARD_VIRT)
static int shell_mailbox_property_u32(UInt tag, UInt* value0, UInt* value1) {
    (void)tag;
    (void)value0;
    (void)value1;
    return -1;
}
#else
static int shell_mailbox_property_u32(UInt tag, UInt* value0, UInt* value1) {
    mailbox_buffer[0] = 8 * 4;
    mailbox_buffer[1] = MBOX_REQUEST;
    mailbox_buffer[2] = tag;
    mailbox_buffer[3] = 8;
    mailbox_buffer[4] = 0;
    mailbox_buffer[5] = 0;
    mailbox_buffer[6] = 0;
    mailbox_buffer[7] = MBOX_TAG_LAST;

    if (!mbox_call(MBOX_CH_PROP)) {
        return -1;
    }

    if (value0) {
        *value0 = mailbox_buffer[5];
    }
    if (value1) {
        *value1 = mailbox_buffer[6];
    }

    return 0;
}
#endif

static void shell_show_hardware_info(void) {
    const TouchState* touch_state = touch_get_state();
    UInt serial_low = 0;
    UInt serial_high = 0;
    unsigned long timer_now = get_system_timer();
    unsigned long timer_freq = shell_read_cntfrq_el0();

    shell_print_heading("Hardware Info");
    shell_print_kv("board", "%s",
#if defined(ROS_BOARD_VIRT)
        "QEMU virt target"
#else
        "Raspberry Pi 3 Model B target"
#endif
    );
    shell_print_kv("arch", "%s", "AArch64");
    shell_print_kv("cpu", "%s", "Arm Cortex-A53 class");
    shell_print_kv("cpus", "%u configured, %u online", (unsigned int)NR_CPUS, shell_count_online_cpus());
    shell_print_kv("timer freq", "%lu Hz", timer_freq);
    shell_print_kv("sys timer", "%lu us", timer_now);
    shell_print_kv("sched ticks", "%lu", schedler_get_ticks());

#if defined(ROS_BOARD_VIRT)
    shell_print_kv("serial", "%s", "unavailable");
#else
    if (shell_mailbox_property_u32(MBOX_TAG_GETSERIAL, &serial_low, &serial_high) == 0) {
        shell_print_kv("serial", "0x%08X%08X", serial_high, serial_low);
    }
    else {
        shell_print_kv("serial", "%s", "unavailable");
    }
#endif

    if (graphics_is_ready()) {
        shell_print_kv("framebuffer", "%ux%u pitch=%u %s fb=0x%lX",
            graphics_width(),
            graphics_height(),
            graphics_pitch(),
            graphics_is_rgb() ? "RGB" : "BGR",
            graphics_framebuffer());
    }
    else {
        shell_print_kv("framebuffer", "%s", "not ready");
    }

    if (touch_is_ready() && touch_state) {
        shell_print_kv("touch", "%s contacts=%u pressed=%s pos=(%u,%u)",
            "ready",
            touch_state->contact_count,
            touch_state->pressed ? "yes" : "no",
            touch_state->x,
            touch_state->y);
    }
    else {
        shell_print_kv("touch", "%s", "not ready");
    }
}

static const char* hal_partition_type_name(enum HalPartitionFilesystemType fs_type) {
    switch (fs_type) {
    case HAL_PARTITION_TYPE_NONE: return "none";
    case HAL_PARTITION_TYPE_OTHER: return "other";
    case HAL_PARTITION_TYPE_FAT32: return "FAT32";
    case HAL_PARTITION_TYPE_LINUX: return "LINUX";
    case HAL_PARTITION_TYPE_DATA: return "DATA";
    case HAL_PARTITION_TYPE_ESP: return "ESP";
    default: return "unknown";
    }
}

static void shell_show_devices(void) {
    char number[64];
    int found = 0;

    shell_print_heading("Devices");

    /* Block devices */
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("TYPE", 8, null);
    kprint(" ");
    shell_print_text_column("ID", 6, null);
    kprint(" ");
    shell_print_text_column("INFO", 40, null);
    kprint("\r\n");
    shell_print_separator();

    for (int i = 0; i < HAL_BLOCK_MAX; i++) {
        if (hal_block_map[i].driver) {
            char info[64];
            shell_format_text(info, "driver=0x%lX priv=0x%lX", (unsigned long)hal_block_map[i].driver, (unsigned long)hal_block_map[i].private);
            kprint("  ");
            shell_print_text_column("block", 8, SHELL_COLOR_LABEL);
            kprint(" ");
            shell_format_text(number, "%d", i);
            shell_print_text_column(number, 6, null);
            kprint(" %s\r\n", info);
            found++;
        }
    }
    if (found == 0) {
        kprint("  (no block devices)\r\n");
    }

    /* Partitions */
    shell_print_heading("Partitions");
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("IDX", 6, null);
    kprint(" ");
    shell_print_text_column("DEV", 6, null);
    kprint(" ");
    shell_print_text_column("BEGIN", 12, null);
    kprint(" ");
    shell_print_text_column("SIZE", 12, null);
    kprint(" TYPE" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (int i = 0; i < HAL_PARTITION_MAX; i++) {
        if (hal_partition_map[i].fs_type != HAL_PARTITION_TYPE_NONE) {
            const char* tname = hal_partition_type_name(hal_partition_map[i].fs_type);
            kprint("  ");
            shell_format_text(number, "%d", i);
            shell_print_text_column(number, 6, null);
            kprint(" ");
            shell_format_text(number, "%d", hal_partition_map[i].dev);
            shell_print_text_column(number, 6, null);
            kprint(" ");
            shell_format_text(number, "0x%lX", hal_partition_map[i].begin);
            shell_print_text_column(number, 12, null);
            kprint(" ");
            shell_format_text(number, "0x%lX", hal_partition_map[i].size);
            shell_print_text_column(number, 12, null);
            kprint(" %s\r\n", tname);
        }
    }

    /* Graphics / Input */
    shell_print_heading("Graphics / Input");
    shell_print_kv("framebuffer", "%s", graphics_is_ready() ? "ready" : "not ready");
    if (graphics_is_ready()) {
        shell_print_kv("resolution", "%ux%u pitch=%u", graphics_width(), graphics_height(), graphics_pitch());
        shell_print_kv("fb addr", SHELL_COLOR_PHYS "0x%lX" SHELL_COLOR_RESET, graphics_framebuffer());
    }
    shell_print_kv("touch", "%s", touch_is_ready() ? "ready" : "not ready");
}

static char* shell_skip_spaces(char* text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }

    return text;
}

static char* shell_next_token(char** cursor) {
    char* start = shell_skip_spaces(*cursor);
    char* end = start;

    if (*start == '\0') {
        *cursor = start;
        return null;
    }

    while (*end != '\0' && *end != ' ' && *end != '\t') {
        end++;
    }
    if (*end != '\0') {
        *end = '\0';
        end++;
    }

    *cursor = end;
    return start;
}

static void shell_print_prompt(void) {
    kprint("%s", shell_prompt);
}

static int shell_parse_ulong(const char* text, unsigned long* value) {
    unsigned long result = 0;
    int base = 10;

    if (!text || !value || text[0] == '\0') {
        return -1;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (*text == '\0') {
        return -1;
    }

    while (*text != '\0') {
        unsigned long digit;

        if (*text >= '0' && *text <= '9') {
            digit = (unsigned long)(*text - '0');
        }
        else if (base == 16 && *text >= 'a' && *text <= 'f') {
            digit = (unsigned long)(*text - 'a' + 10);
        }
        else if (base == 16 && *text >= 'A' && *text <= 'F') {
            digit = (unsigned long)(*text - 'A' + 10);
        }
        else {
            return -1;
        }

        if (digit >= (unsigned long)base) {
            return -1;
        }

        result = (result * (unsigned long)base) + digit;
        text++;
    }

    *value = result;
    return 0;
}

static Task* shell_find_task(const char* pid_text) {
    unsigned long pid_value;

    if (!pid_text) {
        return current_task;
    }
    if (shell_parse_ulong(pid_text, &pid_value) != 0 || pid_value >= (unsigned long)NR_TASKS) {
        return null;
    }
    return tasks[pid_value];
}

static int shell_parse_byte(const char* text, UByte* value) {
    unsigned long parsed;

    if (!value || shell_parse_ulong(text, &parsed) != 0 || parsed > 0xFFUL) {
        return -1;
    }

    *value = (UByte)parsed;
    return 0;
}

static int shell_validate_length(unsigned long length) {
    if (length == 0 || length > SHELL_MEM_TRANSFER_MAX) {
        return -1;
    }

    return 0;
}

static int shell_range_last_addr(unsigned long addr, unsigned long length, unsigned long* last) {
    if (!last || length == 0) {
        return -1;
    }
    *last = addr + length - 1;
    if (*last < addr) {
        return -1;
    }

    return 0;
}

static int shell_parse_write_bytes(char* cursor, UByte* bytes, int max_bytes) {
    int count = 0;

    for (;;) {
        char* token = shell_next_token(&cursor);

        if (!token) {
            break;
        }
        if (count >= max_bytes || shell_parse_byte(token, &bytes[count]) != 0) {
            return -1;
        }
        count++;
    }

    return count;
}

static void shell_print_write_result(const char* scope, Task* task, unsigned long addr, const UByte* bytes, int count, Bool physical) {
    int index;

    shell_print_heading("Memory Write");
    shell_print_kv("scope", "%s", scope);
    if (task) {
        shell_print_kv("task", "%d", task->id);
    }
    shell_print_kv(physical ? "phys" : "virt", "%s0x%lX%s", physical ? SHELL_COLOR_PHYS : SHELL_COLOR_VIRT, addr, SHELL_COLOR_RESET);
    kprint("  ");
    shell_print_text_column("bytes", 14, SHELL_COLOR_LABEL);
    kprint(" " SHELL_COLOR_MUTED "|" SHELL_COLOR_RESET " " SHELL_COLOR_VALUE);
    for (index = 0; index < count; index++) {
        if (index != 0) {
            kprint(" ");
        }
        kprint("%02X", bytes[index]);
    }
    kprint(SHELL_COLOR_RESET "\r\n");
}

static void shell_dump_host_memory(unsigned long addr, unsigned long length, Bool physical) {
    unsigned long last;
    Address mapped;

    if (shell_validate_length(length) != 0) {
        shell_print_error("Length must be between 1 and %lu bytes", SHELL_MEM_TRANSFER_MAX);
        return;
    }
    if (shell_range_last_addr(addr, length, &last) != 0) {
        shell_print_error("Address range overflows: 0x%lX + %lu bytes", addr, length);
        return;
    }

    if (!physical && mem_is_kernel_virt_addr((VirtAddr)addr) && mem_is_kernel_virt_addr((VirtAddr)last)) {
        mapped = (Address)addr;
    }
    else if (mem_is_valid_phys_addr((PhysAddr)addr) && mem_is_valid_phys_addr((PhysAddr)last)) {
        mapped = (Address)mem_phys_to_virt((PhysAddr)addr);
        physical = true;
    }
    else {
        shell_print_error("Invalid memory address: 0x%lX", addr);
        return;
    }

    shell_print_heading("Memory Dump");
    shell_print_kv("scope", "%s", physical ? "physical" : "kernel-virtual");
    shell_print_kv(physical ? "phys" : "virt", "%s0x%lX%s", physical ? SHELL_COLOR_PHYS : SHELL_COLOR_VIRT, addr, SHELL_COLOR_RESET);
    shell_print_kv("length", "%lu bytes", length);
    kdump_region((void*)mapped, (int)length, addr);
}

static void shell_dump_memory(const char* addr_text, const char* length_text) {
    unsigned long addr;
    unsigned long length = 128;

    if (!addr_text || shell_parse_ulong(addr_text, &addr) != 0) {
        shell_print_usage("mem <addr> [len]");
        return;
    }
    if (length_text && shell_parse_ulong(length_text, &length) != 0) {
        shell_print_usage("mem <addr> [len]");
        return;
    }

    shell_dump_host_memory(addr, length, false);
}

static void shell_print_task_memory_pages(Task* task, Address start_va, unsigned long length) {
    Address page_va = start_va & MM_PAGE_MASK;
    Address end_va = start_va + length;

    if (end_va < start_va) {
        return;
    }

    kprint("  ");
    shell_print_text_column("pages", 14, SHELL_COLOR_LABEL);
    kprint(" " SHELL_COLOR_MUTED "|" SHELL_COLOR_RESET "\r\n");

    for (; page_va < end_va; page_va += PAGE_SIZE) {
        MmuWalkResult walk;

        kprint("                 ");
        if (process_walk_page(task, page_va, &walk) != 0) {
            kprint("VA " SHELL_COLOR_VIRT "0x%lX" SHELL_COLOR_RESET " -> <unmapped>\r\n", page_va);
            continue;
        }

        kprint("VA " SHELL_COLOR_VIRT "0x%lX" SHELL_COLOR_RESET " => PA " SHELL_COLOR_PHYS "0x%lX" SHELL_COLOR_RESET "\r\n",
            walk.virt_addr,
            walk.phys_addr);
    }
}

static void shell_dump_task_memory(const char* pid_text, const char* va_text, const char* length_text) {
    Task* task;
    unsigned long va;
    unsigned long length = 128;
    void* snapshot;

    if (!pid_text || !va_text) {
        shell_print_usage("mem task <pid> <va> [len]");
        return;
    }

    task = shell_find_task(pid_text);
    if (!task) {
        shell_print_error("Task not found");
        return;
    }
    if (shell_parse_ulong(va_text, &va) != 0) {
        shell_print_error("Invalid virtual address: %s", va_text);
        return;
    }
    if (length_text && shell_parse_ulong(length_text, &length) != 0) {
        shell_print_usage("mem task <pid> <va> [len]");
        return;
    }
    if (shell_validate_length(length) != 0) {
        shell_print_error("Length must be between 1 and %lu bytes", SHELL_MEM_TRANSFER_MAX);
        return;
    }

    snapshot = (void*)kmalloc((int)length);
    if (!snapshot) {
        shell_print_error("Failed to allocate %lu-byte snapshot buffer", length);
        return;
    }
    if (process_copy_from_user(task, (Address)va, snapshot, length) != 0) {
        kfree((Address)snapshot);
        shell_print_error("Task %d has no fully mapped range at VA 0x%lX", task->id, va);
        return;
    }

    shell_print_heading("Task Memory Dump");
    shell_print_kv("task", "%d", task->id);
    shell_print_kv("name", "%s", shell_task_display_name(task));
    shell_print_kv("pgd", SHELL_COLOR_PHYS "0x%lX" SHELL_COLOR_RESET, task->mm.pgd);
    shell_print_kv("virt", "%s0x%lX%s", SHELL_COLOR_VIRT, va, SHELL_COLOR_RESET);
    shell_print_kv("length", "%lu bytes", length);
    shell_print_task_memory_pages(task, (Address)va, length);
    kdump_region(snapshot, (int)length, va);
    kfree((Address)snapshot);
}

static void shell_write_host_memory(unsigned long addr, const UByte* bytes, int count, Bool physical) {
    unsigned long last;
    Address mapped;

    if (count <= 0) {
        shell_print_error("No bytes supplied");
        return;
    }
    if (shell_range_last_addr(addr, (unsigned long)count, &last) != 0) {
        shell_print_error("Address range overflows: 0x%lX + %d bytes", addr, count);
        return;
    }

    if (!physical && mem_is_kernel_virt_addr((VirtAddr)addr) && mem_is_kernel_virt_addr((VirtAddr)last)) {
        mapped = (Address)addr;
    }
    else if (mem_is_valid_phys_addr((PhysAddr)addr) && mem_is_valid_phys_addr((PhysAddr)last)) {
        mapped = (Address)mem_phys_to_virt((PhysAddr)addr);
        physical = true;
    }
    else {
        shell_print_error("Invalid memory address: 0x%lX", addr);
        return;
    }

    memmove((void*)mapped, bytes, (unsigned int)count);
    shell_print_write_result(physical ? "physical" : "kernel-virtual", null, addr, bytes, count, physical);
}

static void shell_write_memory(const char* addr_text, char* cursor) {
    UByte bytes[SHELL_WRITE_MAX_BYTES];
    unsigned long addr;
    int count;

    if (!addr_text || shell_parse_ulong(addr_text, &addr) != 0) {
        shell_print_usage("mwrite <addr> <byte...>");
        return;
    }

    count = shell_parse_write_bytes(cursor, bytes, SHELL_WRITE_MAX_BYTES);
    if (count <= 0) {
        shell_print_usage("mwrite <addr> <byte...>");
        return;
    }

    shell_write_host_memory(addr, bytes, count, false);
}

static void shell_write_task_memory(const char* pid_text, const char* va_text, char* cursor) {
    Task* task;
    UByte bytes[SHELL_WRITE_MAX_BYTES];
    unsigned long va;
    int count;

    if (!pid_text || !va_text) {
        shell_print_usage("mwrite task <pid> <va> <byte...>");
        return;
    }

    task = shell_find_task(pid_text);
    if (!task) {
        shell_print_error("Task not found");
        return;
    }
    if (shell_parse_ulong(va_text, &va) != 0) {
        shell_print_error("Invalid virtual address: %s", va_text);
        return;
    }

    count = shell_parse_write_bytes(cursor, bytes, SHELL_WRITE_MAX_BYTES);
    if (count <= 0) {
        shell_print_usage("mwrite task <pid> <va> <byte...>");
        return;
    }
    if (process_copy_to_user(task, (Address)va, bytes, (ULong)count) != 0) {
        shell_print_error("Task %d has no fully mapped range at VA 0x%lX", task->id, va);
        return;
    }

    shell_print_write_result("task-virtual", task, va, bytes, count, false);
}

static void shell_show_task(const char* pid_text) {
    Task* task = shell_find_task(pid_text);

    if (!task) {
        shell_print_error("Task not found");
        return;
    }

    process_dump_task_struct(task);
}

static void shell_show_maps(const char* pid_text) {
    Task* task = shell_find_task(pid_text);

    if (!task) {
        shell_print_error("Task not found");
        return;
    }

    process_dump_mappings(task);
}

static void shell_show_pte(const char* pid_text, const char* va_text) {
    Task* task;
    MmuWalkResult walk;
    unsigned long va;

    if (!va_text) {
        shell_print_usage("pte <pid> <va>");
        return;
    }

    task = shell_find_task(pid_text);
    if (!task) {
        shell_print_error("Task not found");
        return;
    }
    if (shell_parse_ulong(va_text, &va) != 0) {
        shell_print_error("Invalid virtual address: %s", va_text);
        return;
    }
    if (process_walk_page(task, (Address)va, &walk) != 0) {
        shell_print_error("No mapping for task %d at 0x%lX", task->id, va & MM_PAGE_MASK);
        return;
    }

    shell_print_heading("Page Table Walk");
    shell_print_kv("task", "%d", task->id);
    shell_print_kv("virt", SHELL_COLOR_PATH "0x%lX" SHELL_COLOR_RESET, walk.virt_addr);
    shell_print_kv("pgd", "0x%lX  [%d]", walk.pgd, walk.pgd_index);
    shell_print_kv("pud", "0x%lX  [%d]", walk.pud, walk.pud_index);
    shell_print_kv("pmd", "0x%lX  [%d]", walk.pmd, walk.pmd_index);
    shell_print_kv("pte", "0x%lX  [%d]", walk.pte, walk.pte_index);
    shell_print_kv("phys", SHELL_COLOR_PATH "0x%lX" SHELL_COLOR_RESET, walk.phys_addr);
    shell_print_kv("flags", "0x%lX", walk.flags);
    shell_print_kv("refs", "%d", mem_get_page_refcount(walk.phys_addr));
}

static const ModuleSection* shell_module_sections(const ModuleHeader* header) {
    return (const ModuleSection*)(((const UByte*)header) + sizeof(ModuleHeader));
}

static const ModuleImport* shell_module_imports(const ModuleHeader* header) {
    return (const ModuleImport*)(((const UByte*)shell_module_sections(header)) + ((ULong)header->section_count * sizeof(ModuleSection)));
}

static int shell_module_validate_flat_image(const UByte* image, ULong image_size, const ModuleHeader** out_header) {
    const ModuleHeader* header;
    const UByte* metadata_end;
    const ModuleSection* sections;
    const ModuleImport* imports;
    ULong cursor;
    UInt index;

    if (!image || image_size < sizeof(ModuleHeader)) {
        return -1;
    }

    header = (const ModuleHeader*)image;
    if (header->magic[0] != MOD_MAGIC_0 ||
        header->magic[1] != MOD_MAGIC_1 ||
        header->magic[2] != MOD_MAGIC_2 ||
        header->magic[3] != MOD_MAGIC_3 ||
        header->magic[4] != MOD_MAGIC_4 ||
        header->magic[5] != MOD_MAGIC_5 ||
        header->magic[6] != MOD_MAGIC_6 ||
        header->magic[7] != MOD_MAGIC_7) {
        return -1;
    }
    if (header->abi_version != MOD_ABI_VERSION || header->machine != MOD_MACHINE_AARCH64) {
        return -1;
    }
    if (header->header_size < sizeof(ModuleHeader) || header->header_size > image_size) {
        return -1;
    }
    if (header->section_count == 0 || header->section_count > 16U) {
        return -1;
    }
    if (header->align == 0 || !mem_is_page_aligned(header->align)) {
        return -1;
    }
    if (header->image_size == 0 || !mem_is_page_aligned(header->image_size)) {
        return -1;
    }

    cursor = sizeof(ModuleHeader);
    if (cursor + ((ULong)header->section_count * sizeof(ModuleSection)) > header->header_size) {
        return -1;
    }
    sections = (const ModuleSection*)(image + cursor);
    cursor += (ULong)header->section_count * sizeof(ModuleSection);

    if (cursor + ((ULong)header->import_count * sizeof(ModuleImport)) > header->header_size) {
        return -1;
    }
    imports = (const ModuleImport*)(image + cursor);
    metadata_end = image + header->header_size;

    for (index = 0; index < header->section_count; index++) {
        const ModuleSection* section = &sections[index];

        if (section->type > MOD_SEC_DATA) {
            return -1;
        }
        if (section->align == 0) {
            return -1;
        }
        if (section->runtime_offset + section->mem_size < section->runtime_offset) {
            return -1;
        }
        if (section->runtime_offset + section->mem_size > header->image_size) {
            return -1;
        }
        if (section->file_size > section->mem_size) {
            return -1;
        }
        if (section->file_offset < header->header_size) {
            return -1;
        }
        if (section->file_offset + section->file_size > image_size) {
            return -1;
        }
    }

    for (index = 0; index < header->import_count; index++) {
        const ModuleImport* entry = &imports[index];
        const char* name;

        if (entry->type != MOD_IMPORT_ABS64 && entry->type != MOD_IMPORT_REL64) {
            return -1;
        }
        if (entry->name_offset >= header->header_size) {
            return -1;
        }
        name = (const char*)(image + entry->name_offset);
        while ((const UByte*)name < metadata_end && *name != '\0') {
            name++;
        }
        if ((const UByte*)name >= metadata_end) {
            return -1;
        }
    }

    if (out_header) {
        *out_header = header;
    }
    return 0;
}

static const char* shell_module_section_type_name(UInt type) {
    if (type == MOD_SEC_TEXT) {
        return "text";
    }
    if (type == MOD_SEC_RODATA) {
        return "rodata";
    }
    if (type == MOD_SEC_DATA) {
        return "data";
    }
    return "unknown";
}

static void shell_module_format_flags(char* buffer, UInt flags) {
    buffer[0] = (flags & MOD_SECTION_FLAG_READ) ? 'R' : '-';
    buffer[1] = (flags & MOD_SECTION_FLAG_WRITE) ? 'W' : '-';
    buffer[2] = (flags & MOD_SECTION_FLAG_EXEC) ? 'X' : '-';
    buffer[3] = '\0';
}

static const char* shell_module_import_type_name(UInt type) {
    return type == MOD_IMPORT_REL64 ? "REL64" : "ABS64";
}

static void shell_show_module_bundle_detail(const KernelModuleInfo* info) {
    struct FileDesc fd;
    ModuleBundleHeader bundle;
    UByte* module_image = null;
    const ModuleHeader* header = null;
    const ModuleSection* sections;
    const ModuleImport* imports;
    KernelModuleRuntimeInfo runtime;
    UInt module_offset;
    Bool fd_open = false;
    UInt index;

    if (!info || vfs_fd_open(&fd, info->path, O_READ) != SUCCESS) {
        shell_print_error("Failed to open module bundle: %s", info ? info->path : "<null>");
        return;
    }
    fd_open = true;

    if (module_read_header(&fd, &bundle) != 0 || module_validate_header(&bundle) != 0) {
        shell_print_error("Failed to read module bundle header: %s", info->path);
        goto cleanup;
    }

    module_offset = (UInt)(((bundle.header_size + bundle.manifest_size) + bundle.payload_align - 1U) & ~(bundle.payload_align - 1U));
    module_image = (UByte*)kmalloc((int)bundle.module_size);
    if (!module_image) {
        shell_print_error("Out of memory while reading module bundle");
        goto cleanup;
    }
    if (vfs_fd_seek(&fd, module_offset, SEEK_SET) < 0 || vfs_fd_read(&fd, module_image, bundle.module_size) != (int)bundle.module_size) {
        shell_print_error("Failed to read flat module payload");
        goto cleanup;
    }
    if (shell_module_validate_flat_image(module_image, bundle.module_size, &header) != 0) {
        shell_print_error("Flat module payload is invalid");
        goto cleanup;
    }

    module_runtime_get(info->name, &runtime);
    sections = shell_module_sections(header);
    imports = shell_module_imports(header);

    shell_print_heading("Callbacks");
    shell_print_kv("init", "section=%u offset=0x%lX va=0x%lX", bundle.init_section, bundle.init_offset, runtime.init_va);
    if (bundle.shutdown_section != MOD_INVALID_SECTION) {
        shell_print_kv("shutdown", "section=%u offset=0x%lX va=0x%lX", bundle.shutdown_section, bundle.shutdown_offset, runtime.shutdown_va);
    }
    else {
        shell_print_kv("shutdown", "<none>");
    }
    if (bundle.idle_section != MOD_INVALID_SECTION) {
        shell_print_kv("idle", "section=%u offset=0x%lX va=0x%lX", bundle.idle_section, bundle.idle_offset, runtime.idle_va);
    }
    else {
        shell_print_kv("idle", "<none>");
    }
    shell_print_kv("base", "0x%lX", runtime.base_va);
    shell_print_kv("bias", "0x%lX", runtime.load_bias);

    shell_print_heading("Sections");
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("IDX", 6, null);
    kprint(" ");
    shell_print_text_column("TYPE", 10, null);
    kprint(" ");
    shell_print_text_column("FLAGS", 6, null);
    kprint(" ");
    shell_print_text_column("RVA", 12, null);
    kprint(" ");
    shell_print_text_column("VA", 20, null);
    kprint(" ");
    shell_print_text_column("FILE", 10, null);
    kprint(" MEM" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (index = 0; index < header->section_count; index++) {
        char number[32];
        char flags[4];

        kprint("  ");
        shell_format_text(number, "%u", index);
        shell_print_text_column(number, 6, null);
        kprint(" ");
        shell_print_text_column(shell_module_section_type_name(sections[index].type), 10, null);
        kprint(" ");
        shell_module_format_flags(flags, sections[index].flags);
        shell_print_text_column(flags, 6, null);
        kprint(" ");
        shell_format_text(number, "0x%lX", sections[index].runtime_offset);
        shell_print_text_column(number, 12, null);
        kprint(" ");
        shell_format_text(number, "0x%lX", runtime.base_va + sections[index].runtime_offset);
        shell_print_text_column(number, 20, SHELL_COLOR_VALUE);
        kprint(" ");
        shell_format_text(number, "%lu", sections[index].file_size);
        shell_print_text_column(number, 10, null);
        kprint(" ");
        shell_format_text(number, "%lu", sections[index].mem_size);
        kprint("%s\r\n", number);
    }

    shell_print_heading("Imports");
    shell_print_kv("count", "%u", header->import_count);
    if (header->import_count == 0) {
        shell_print_kv("imports", "<none>");
        goto cleanup;
    }
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("IDX", 6, null);
    kprint(" ");
    shell_print_text_column("TYPE", 8, null);
    kprint(" ");
    shell_print_text_column("PATCH", 14, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (index = 0; index < header->import_count; index++) {
        char number[32];
        const char* import_name = (const char*)(module_image + imports[index].name_offset);

        kprint("  ");
        shell_format_text(number, "%u", index);
        shell_print_text_column(number, 6, null);
        kprint(" ");
        shell_print_text_column(shell_module_import_type_name(imports[index].type), 8, null);
        kprint(" ");
        shell_format_text(number, "0x%lX", imports[index].patch_offset);
        shell_print_text_column(number, 14, null);
        kprint(" %s\r\n", import_name);
    }

cleanup:
    if (fd_open) {
        vfs_fd_close(&fd);
    }
    if (module_image) {
        kfree((Address)module_image);
    }
}

static const char* shell_module_state_name(ModuleState state) {
    if (state == MODULE_STATE_DISCOVERED) {
        return "DISCOVERED";
    }
    if (state == MODULE_STATE_READY) {
        return "READY";
    }
    if (state == MODULE_STATE_FAILED) {
        return "FAILED";
    }
    return "EMPTY";
}

static const char* shell_module_state_color(ModuleState state) {
    if (state == MODULE_STATE_READY) {
        return SHELL_COLOR_OK;
    }
    if (state == MODULE_STATE_DISCOVERED) {
        return SHELL_COLOR_WARN;
    }
    if (state == MODULE_STATE_FAILED) {
        return SHELL_COLOR_ERROR;
    }
    return SHELL_COLOR_MUTED;
}

static void shell_show_modules(const char* name) {
    if (name && name[0] != '\0') {
        const KernelModuleInfo* info = module_find(name);
        unsigned int export_count;
        unsigned int index;

        if (!info) {
            shell_print_error("Module not found: %s", name);
            return;
        }

        shell_print_heading("Kernel Module");
        shell_print_kv("name", "%s", info->name);
        shell_print_kv("state", "%s%s%s", shell_module_state_color(info->state), shell_module_state_name(info->state), SHELL_COLOR_RESET);
        shell_print_kv("path", "%s%s%s", SHELL_COLOR_PATH, info->path, SHELL_COLOR_RESET);
        shell_print_kv("flags", "0x%X", info->flags);
        shell_print_kv("image", "%lu bytes", info->image_size);
        shell_print_kv("bss", "%lu bytes", info->bss_size);

        export_count = module_export_count(info->name);
        shell_print_kv("exports", "%u", export_count);
        if (info->state != MODULE_STATE_READY) {
            shell_print_kv("note", "exports are only available after a module reaches READY");
            return;
        }

        shell_show_module_bundle_detail(info);

        shell_print_heading("Export Table");
        kprint(SHELL_COLOR_MUTED "  ");
        shell_print_text_column("INDEX", 8, null);
        kprint(" ");
        shell_print_text_column("ADDRESS", 20, null);
        kprint(" NAME" SHELL_COLOR_RESET "\r\n");
        shell_print_separator();

        if (export_count == 0) {
            kprint("  (no exports)\r\n");
            return;
        }

        for (index = 0; index < export_count; index++) {
            KernelModuleExportInfo export_info;
            char number[32];

            if (module_export_get(info->name, index, &export_info) != 0) {
                continue;
            }

            kprint("  ");
            shell_format_text(number, "%u", index);
            shell_print_text_column(number, 8, null);
            kprint(" ");
            shell_format_text(number, "0x%lX", export_info.address);
            shell_print_text_column(number, 20, SHELL_COLOR_VALUE);
            kprint(" %s\r\n", export_info.name);
        }
        return;
    }

    {
        unsigned int count = module_count();
        unsigned int index;

        shell_print_heading("Kernel Modules");
        shell_print_kv("count", "%u", count);
        kprint(SHELL_COLOR_MUTED "  ");
        shell_print_text_column("STATE", 12, null);
        kprint(" ");
        shell_print_text_column("EXPORTS", 8, null);
        kprint(" ");
        shell_print_text_column("IMAGE", 12, null);
        kprint(" ");
        shell_print_text_column("BSS", 10, null);
        kprint(" ");
        shell_print_text_column("NAME", 18, null);
        kprint(" PATH" SHELL_COLOR_RESET "\r\n");
        shell_print_separator();

        if (count == 0) {
            kprint("  (no modules)\r\n");
            return;
        }

        for (index = 0; index < count; index++) {
            const KernelModuleInfo* info = module_get_at(index);
            char number[32];

            if (!info) {
                continue;
            }

            kprint("  ");
            shell_print_text_column(shell_module_state_name(info->state), 12, shell_module_state_color(info->state));
            kprint(" ");
            shell_format_text(number, "%u", module_export_count(info->name));
            shell_print_text_column(number, 8, null);
            kprint(" ");
            shell_format_text(number, "%lu", info->image_size);
            shell_print_text_column(number, 12, null);
            kprint(" ");
            shell_format_text(number, "%lu", info->bss_size);
            shell_print_text_column(number, 10, null);
            kprint(" ");
            shell_print_text_column(info->name, 18, SHELL_COLOR_VALUE);
            kprint(" %s%s%s\r\n", SHELL_COLOR_PATH, info->path, SHELL_COLOR_RESET);
        }
    }
}

static void shell_show_shared_libraries(void) {
    UserSharedLibraryInfo libraries[USER_SHARED_LIBRARY_MAX_LOADED];
    UserSharedLibraryExportInfo exports[USER_SHARED_LIBRARY_MAX_EXPORTS];
    UInt library_count;

    memzero((Address)libraries, sizeof(libraries));
    library_count = user_shared_library_snapshot(libraries, USER_SHARED_LIBRARY_MAX_LOADED);

    shell_print_heading("Loaded User Modules");
    shell_print_kv("count", "%u", library_count);

    if (library_count == 0) {
        shell_print_kv("modules", "<none>");
        return;
    }

    for (UInt index = 0; index < library_count; index++) {
        const UserSharedLibraryInfo* library = &libraries[index];
        const char* kind = "UNKNOWN";
        UInt export_count;

        if (library->kind == LDR_IMAGE_DLL) {
            kind = "DLL";
        }
        else if (library->kind == LDR_IMAGE_SYS) {
            kind = "DRIVER";
        }

        kprint("  %s%s%s\r\n", SHELL_COLOR_PATH, library->path, SHELL_COLOR_RESET);
        shell_print_kv("kind", "%s", kind);
        shell_print_kv("base", "0x%lX", library->base_va);
        shell_print_kv("entry", "0x%lX", library->entry_point);
        shell_print_kv("image", "%lu bytes", library->image_size);
        shell_print_kv("pages", "%u", library->page_count);
        shell_print_kv("refs", "%u", library->ref_count);
        if (library->kind == LDR_IMAGE_SYS) {
            shell_print_kv("driver loop", "%s", library->driver_loop_active ? "active" : "stopped");
        }

        memzero((Address)exports, sizeof(exports));
        export_count = user_shared_library_export_snapshot(library->path, exports, USER_SHARED_LIBRARY_MAX_EXPORTS);
        shell_print_kv("exports", "%u", export_count);

        if (export_count == 0) {
            shell_print_kv("export table", "<none>");
            continue;
        }

        kprint(SHELL_COLOR_MUTED "    ");
        shell_print_text_column("IDX", 6, null);
        kprint(" ");
        shell_print_text_column("ADDRESS", 20, null);
        kprint(" NAME" SHELL_COLOR_RESET "\r\n");
        shell_print_separator();

        for (UInt export_index = 0; export_index < export_count; export_index++) {
            char number[32];

            kprint("    ");
            shell_format_text(number, "%u", export_index);
            shell_print_text_column(number, 6, null);
            kprint(" ");
            shell_format_text(number, "0x%lX", exports[export_index].address);
            shell_print_text_column(number, 20, SHELL_COLOR_VALUE);
            kprint(" %s\r\n", exports[export_index].name);
        }
    }
}

static void shell_print_help(void) {
    shell_print_heading("Kernel Shell Commands");
    kprint("  "); shell_print_text_column("help", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show this help\r\n");
    kprint("  "); shell_print_text_column("exit", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Halt the machine\r\n");
    kprint("  "); shell_print_text_column("cpu [id]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show CPU overview or one CPU in detail\r\n");
    kprint("  "); shell_print_text_column("sched", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show scheduler counters and per-CPU load\r\n");
    kprint("  "); shell_print_text_column("usage [samples] [delay_ms]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show CPU and memory usage snapshot\r\n");
    kprint("  "); shell_print_text_column("top [samples] [delay_ms]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Refresh compact task view\r\n");
    kprint("  "); shell_print_text_column("mpstat [samples] [delay_ms]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Refresh compact per-CPU scheduler view\r\n");
    kprint("  "); shell_print_text_column("hwinfo", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show board and device status\r\n");
    kprint("  "); shell_print_text_column("lsdev", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" List registered devices\r\n");
    kprint("  "); shell_print_text_column("ls [path]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" List a directory\r\n");
    kprint("  "); shell_print_text_column("cat <path>", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Print a text file\r\n");
    kprint("  "); shell_print_text_column("run <path> [name]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Spawn a user program\r\n");
    kprint("  "); shell_print_text_column("modules [name]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" List modules or show one export table\r\n");
    kprint("  "); shell_print_text_column("libs", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" List loaded user DLLs/drivers and export tables\r\n");
    kprint("  "); shell_print_text_column("ps", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" List tasks\r\n");
    kprint("  "); shell_print_text_column("ptree", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show the process tree with library details\r\n");
    kprint("  "); shell_print_text_column("inspect", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Dump all processes and tasks in detail\r\n");
    kprint("  "); shell_print_text_column("task [pid]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show detailed task state\r\n");
    kprint("  "); shell_print_text_column("maps [pid]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Dump tracked MMU mappings for a task\r\n");
    kprint("  "); shell_print_text_column("pte <pid> <va>", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Walk one virtual address through page tables\r\n");
    kprint("  "); shell_print_text_column("mem <addr> [len]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Dump memory (kernel VA or physical address)\r\n");
    kprint("  "); shell_print_text_column("mem phys <pa> [len]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Dump physical memory\r\n");
    kprint("  "); shell_print_text_column("mem virt <va> [len]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Dump kernel virtual memory\r\n");
    kprint("  "); shell_print_text_column("mem task <pid> <va> [len]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Dump one task virtual range\r\n");
    kprint("  "); shell_print_text_column("mwrite <addr> <byte...>", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Write bytes to kernel VA or physical memory\r\n");
    kprint("  "); shell_print_text_column("mwrite phys|virt|task ...", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Write bytes to a scoped memory target\r\n");
    kprint("  "); shell_print_text_column("mmutest", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Run MMU self-test\r\n");
    kprint("  "); shell_print_text_column("heaptest [loops]", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Run kernel heap allocate/free/realloc self-test\r\n");
    kprint("  "); shell_print_text_column("ticks", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Show scheduler ticks\r\n");
    kprint("  "); shell_print_text_column("clear", SHELL_HELP_COL_WIDTH, SHELL_COLOR_LABEL); kprint(" Clear the serial screen\r\n");
    shell_print_heading("Examples");
    kprint("  " SHELL_COLOR_PATH "cpu" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "cpu 1" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "sched" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "usage" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "usage 5 1000" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "top 5 1000" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "mpstat 10 500" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "hwinfo" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "ls /" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "ls /bin" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "cat /README.TXT" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "run /bin/sayhello.exe demo" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "run /bin/dllc.exe" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "modules" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "modules sample_sys" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "libs" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "inspect" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "maps 1" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "pte 1 0x4000" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "mem 0x1000 64" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "mem task 1 0x4000 64" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "mwrite phys 0x1000 0x41 0x42 0x43" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "mwrite task 1 0x4000 0x90 0x90" SHELL_COLOR_RESET "\r\n");
    kprint("  " SHELL_COLOR_PATH "heaptest 10" SHELL_COLOR_RESET "\r\n");
}

static void shell_print_task_table(void) {
    char number[32];

    shell_print_heading("Task Table");
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("PID", SHELL_PS_ID_WIDTH, null);
    kprint(" ");
    shell_print_text_column("STATE", SHELL_PS_STATE_WIDTH, null);
    kprint(" ");
    shell_print_text_column("COUNTER", SHELL_PS_COUNT_WIDTH, null);
    kprint(" ");
    shell_print_text_column("CPU", 4, null);
    kprint(" ");
    shell_print_text_column("PRIORITY", SHELL_PS_PRIORITY_WIDTH, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();

    for (int index = 0; index < NR_TASKS; index++) {
        Task* task = tasks[index];
        const char* state;
        const char* name;
        const char* state_color;

        if (!task) {
            continue;
        }

        state = shell_task_state_text(task->state);
        state_color = shell_task_state_color(state);

        name = shell_task_display_name(task);

        kprint("  ");
        shell_format_text(number, "%d", task->id);
        shell_print_text_column(number, SHELL_PS_ID_WIDTH, null);
        kprint(" ");
        shell_print_text_column(state, SHELL_PS_STATE_WIDTH, state_color);
        kprint(" ");
        shell_format_text(number, "%d", task->counter);
        shell_print_text_column(number, SHELL_PS_COUNT_WIDTH, null);
        kprint(" ");
        shell_format_text(number, "%u", task->cpu_affinity);
        shell_print_text_column(number, 4, null);
        kprint(" ");
        shell_format_text(number, "%d", task->priority);
        shell_print_text_column(number, SHELL_PS_PRIORITY_WIDTH, null);
        kprint(" %s\r\n", name);
    }
}

static void shell_tree_line_prefix(int depth) {
    for (int level = 0; level < depth; level++) {
        kprint("| ");
    }
    kprint("+- ");
}

static void shell_tree_detail_prefix(int depth) {
    for (int level = 0; level < depth; level++) {
        kprint("| ");
    }
    kprint("   ");
}

static const UserSharedLibraryInfo* shell_find_loaded_library(const UserSharedLibraryInfo* infos, UInt count, const char* path) {
    if (!infos || !path || path[0] == '\0') {
        return null;
    }

    for (UInt index = 0; index < count; index++) {
        if (infos[index].used && strncmp(infos[index].path, path, USER_SHARED_LIBRARY_PATH_MAX) == 0) {
            return &infos[index];
        }
    }

    return null;
}

static const char* shell_process_parent_text(Process* process, char* buffer, int size) {
    if (!process || process->parent_process_id <= 0 || process->parent_process_id >= NR_PROCESSES || !processes[process->parent_process_id]) {
        strncpy(buffer, "-", size - 1);
        buffer[size - 1] = '\0';
        return buffer;
    }

    shell_format_text(buffer, "%ld", process->parent_process_id);
    return buffer;
}

static void shell_print_library_local(const UserSharedLibraryInfo* infos, UInt info_count, Task* owner_task, const UserSharedLibraryLocal* local, int depth) {
    char number[32];
    UserSharedLibraryImportInfo imports[USER_SHARED_LIBRARY_MAX_IMPORT_REFS];
    const UserSharedLibraryInfo* global = shell_find_loaded_library(infos, info_count, local->path);
    UInt import_count = 0;

    shell_tree_line_prefix(depth);
    kprint("library %s\r\n", local->path);

    shell_tree_detail_prefix(depth + 1);
    kprint("path: %s\r\n", local->path);
    shell_tree_detail_prefix(depth + 1);
    kprint("base: 0x%lX\r\n", local->virt_addr);
    shell_tree_detail_prefix(depth + 1);
    kprint("size: %lu bytes\r\n", local->size);
    shell_tree_detail_prefix(depth + 1);
    kprint("pages: %lu\r\n", local->page_count);

    if (owner_task) {
        memzero((Address)imports, sizeof(imports));
        import_count = user_shared_library_import_snapshot(owner_task->id, local->path, imports, USER_SHARED_LIBRARY_MAX_IMPORT_REFS);
    }

    if (global) {
        shell_tree_detail_prefix(depth + 1);
        kprint("loaded base: 0x%lX\r\n", global->base_va);
        shell_tree_detail_prefix(depth + 1);
        kprint("entry: 0x%lX\r\n", global->entry_point);
        shell_tree_detail_prefix(depth + 1);
        kprint("image: %lu bytes\r\n", global->image_size);
        shell_tree_detail_prefix(depth + 1);
        shell_format_text(number, "%u", global->page_count);
        kprint("loaded pages: %s\r\n", number);
    }

    shell_tree_detail_prefix(depth + 1);
    kprint("references: %u\r\n", import_count);
    if (import_count == 0) {
        shell_tree_detail_prefix(depth + 2);
        kprint("<none>\r\n");
    }
    else {
        UInt visible_count = import_count > USER_SHARED_LIBRARY_MAX_IMPORT_REFS ? USER_SHARED_LIBRARY_MAX_IMPORT_REFS : import_count;

        for (UInt import_index = 0; import_index < visible_count; import_index++) {
            shell_tree_detail_prefix(depth + 2);
            kprint("%s -> %s (iat=0x%lX)\r\n",
                imports[import_index].importer_path[0] ? imports[import_index].importer_path : "<unknown>",
                imports[import_index].symbol_name[0] ? imports[import_index].symbol_name : "<unknown>",
                imports[import_index].iat_address);
        }
        if (import_count > visible_count) {
            shell_tree_detail_prefix(depth + 2);
            kprint("... %u more\r\n", import_count - visible_count);
        }
    }

    for (ULong page = 0; page < local->page_count; page++) {
        shell_tree_detail_prefix(depth + 1);
        kprint("page[%lu]: 0x%lX - 0x%lX\r\n", page,
            local->virt_addr + (page * PAGE_SIZE),
            local->virt_addr + ((page + 1) * PAGE_SIZE) - 1);
    }
}

static void shell_show_task_library_locals(Task* task, const UserSharedLibraryInfo* infos, UInt info_count) {
    Bool found_any = false;

    if (!task) {
        return;
    }

    shell_print_heading("Task Module Mappings");
    for (int local_index = 0; local_index < USER_SHARED_LIBRARY_MAX_TASK_LOCALS; local_index++) {
        const UserSharedLibraryLocal* local = &task->mm.dll_locals[local_index];

        if (local->virt_addr == 0 || local->path[0] == '\0') {
            continue;
        }

        found_any = true;
        shell_print_library_local(infos, info_count, task, local, 0);
    }

    if (!found_any) {
        shell_print_kv("libraries", "%s", "<none mapped>");
    }
}

static void shell_print_process_tree_node(Process* process, const UserSharedLibraryInfo* infos, UInt info_count, Bool* visited, int depth) {
    char parent_text[32];
    Task* task;

    if (!process || process->id < 0 || process->id >= NR_PROCESSES || visited[process->id]) {
        return;
    }
    task = process->main_thread;
    if (!task) {
        return;
    }

    visited[process->id] = true;

    shell_tree_line_prefix(depth);
    kprint("process %ld\r\n", process->id);

    shell_tree_detail_prefix(depth + 1);
    kprint("parent: %s\r\n", shell_process_parent_text(process, parent_text, sizeof(parent_text)));
    shell_tree_detail_prefix(depth + 1);
    kprint("state: %s\r\n", shell_task_state_text(task->state));
    shell_tree_detail_prefix(depth + 1);
    kprint("cpu: %u\r\n", task->cpu_affinity);
    shell_tree_detail_prefix(depth + 1);
    kprint("priority: %ld\r\n", task->priority);
    shell_tree_detail_prefix(depth + 1);
    kprint("counter: %ld\r\n", task->counter);
    shell_tree_detail_prefix(depth + 1);
    kprint("name: %s\r\n", process->name[0] ? process->name : shell_task_display_name(task));
    shell_tree_detail_prefix(depth + 1);
    kprint("program: %s\r\n", process->program_path[0] ? process->program_path : "<none>");
    shell_tree_detail_prefix(depth + 1);
    kprint("args: %s\r\n", process->launch_args[0] ? process->launch_args : "<none>");
    shell_tree_detail_prefix(depth + 1);
    kprint("main thread: %ld\r\n", task->id);
    shell_tree_detail_prefix(depth + 1);
    kprint("threads: %ld\r\n", process->thread_count);
    shell_tree_detail_prefix(depth + 1);
    kprint("pgd: 0x%lX\r\n", task->mm.pgd);
    shell_tree_detail_prefix(depth + 1);
    kprint("heap next: 0x%lX\r\n", task->mm.heap_next);
    shell_tree_detail_prefix(depth + 1);
    kprint("dll next: 0x%lX\r\n", task->mm.dll_local_next);
    shell_tree_detail_prefix(depth + 1);
    kprint("user pages: %d\r\n", task->mm.user_pages_count);
    shell_tree_detail_prefix(depth + 1);
    kprint("kernel pages: %d\r\n", task->mm.kernel_pages_count);
    shell_tree_detail_prefix(depth + 1);
    kprint("stack page: 0x%lX\r\n", task->kernel_stack_page);

    for (int local_index = 0; local_index < USER_SHARED_LIBRARY_MAX_TASK_LOCALS; local_index++) {
        const UserSharedLibraryLocal* local = &task->mm.dll_locals[local_index];

        if (local->virt_addr == 0 || local->path[0] == '\0') {
            continue;
        }

        shell_print_library_local(infos, info_count, task, local, depth + 1);
    }

    for (int index = 0; index < NR_PROCESSES; index++) {
        Process* child = processes[index];

        if (!child || child->parent_process_id != process->id || visited[child->id]) {
            continue;
        }

        shell_print_process_tree_node(child, infos, info_count, visited, depth + 1);
    }
}

static void shell_show_process_tree(void) {
    UserSharedLibraryInfo libraries[USER_SHARED_LIBRARY_MAX_LOADED];
    Bool visited[NR_TASKS];
    UInt library_count;

    memzero((Address)visited, sizeof(visited));
    memzero((Address)libraries, sizeof(libraries));
    library_count = user_shared_library_snapshot(libraries, USER_SHARED_LIBRARY_MAX_LOADED);

    shell_print_heading("Process Tree");
    shell_print_kv("processes", "%d", nr_processes);
    shell_print_kv("threads", "%d", nr_tasks);
    shell_print_kv("loaded libs", "%u", library_count);

    if (library_count > 0) {
        shell_print_heading("Loaded Shared Libraries");
        for (UInt index = 0; index < library_count; index++) {
            kprint("  %s%s%s base=0x%lX entry=0x%lX size=%lu pages=%u\r\n",
                SHELL_COLOR_PATH, libraries[index].path, SHELL_COLOR_RESET,
                libraries[index].base_va, libraries[index].entry_point,
                libraries[index].image_size, libraries[index].page_count);
        }
    }

    shell_print_heading("Tree");
    for (int index = 0; index < NR_PROCESSES; index++) {
        Process* process = processes[index];

        if (!process || visited[process->id]) {
            continue;
        }
        if (process->parent_process_id > 0 && process->parent_process_id < NR_PROCESSES && processes[process->parent_process_id]) {
            continue;
        }

        shell_print_process_tree_node(process, libraries, library_count, visited, 0);
    }

    for (int index = 0; index < NR_TASKS; index++) {
        Task* task = tasks[index];
        Process* process = task ? task->process : 0;

        if (!process || visited[process->id]) {
            continue;
        }

        shell_print_process_tree_node(process, libraries, library_count, visited, 0);
    }
}

static unsigned int shell_count_tasks_for_process(Process* process) {
    unsigned int count = 0;

    if (!process) {
        return 0;
    }

    for (int index = 0; index < NR_TASKS; index++) {
        if (tasks[index] && tasks[index]->process == process) {
            count++;
        }
    }

    return count;
}

static void shell_show_process_detail(Process* process) {
    char parent_text[32];
    unsigned int task_count;

    if (!process) {
        return;
    }

    task_count = shell_count_tasks_for_process(process);

    shell_print_heading("Process Detail");
    shell_print_kv("process", "%ld", process->id);
    shell_print_kv("parent", "%s", shell_process_parent_text(process, parent_text, sizeof(parent_text)));
    shell_print_kv("state", "%s", process->state == PROCESS_ACTIVE ? "ACTIVE" : "ZOMBIE");
    shell_print_kv("name", "%s", process->name[0] ? process->name : "<none>");
    shell_print_kv("program", "%s", process->program_path[0] ? process->program_path : "<none>");
    shell_print_kv("args", "%s", process->launch_args[0] ? process->launch_args : "<none>");
    shell_print_kv("main thread", "%ld", process->main_thread ? process->main_thread->id : -1L);
    shell_print_kv("thread count", "%ld (scan=%u)", process->thread_count, task_count);
}

static void shell_show_system_inspect(void) {
    UserSharedLibraryInfo libraries[USER_SHARED_LIBRARY_MAX_LOADED];
    Bool visited_tasks[NR_TASKS];
    UInt library_count;
    unsigned int process_count = 0;
    unsigned int attached_task_count = 0;

    memzero((Address)libraries, sizeof(libraries));
    memzero((Address)visited_tasks, sizeof(visited_tasks));
    library_count = user_shared_library_snapshot(libraries, USER_SHARED_LIBRARY_MAX_LOADED);

    shell_print_heading("System Inspection");
    shell_print_kv("processes", "%d", nr_processes);
    shell_print_kv("tasks", "%d", nr_tasks);
    shell_print_kv("running", "%u", shell_count_all_tasks_in_state(TASK_RUNNING));
    shell_print_kv("sleeping", "%u", shell_count_all_tasks_in_state(TASK_SLEEPING));
    shell_print_kv("blocked", "%u", shell_count_all_tasks_in_state(TASK_BLOCKED));
    shell_print_kv("zombies", "%u", shell_count_all_tasks_in_state(TASK_ZOMBIE));
    shell_print_kv("online cpus", "%u", shell_count_online_cpus());
    shell_print_kv("loaded libs", "%u", library_count);

    for (int process_index = 0; process_index < NR_PROCESSES; process_index++) {
        Process* process = processes[process_index];
        Bool process_has_tasks = false;

        if (!process) {
            continue;
        }

        process_count++;
        shell_show_process_detail(process);

        for (int task_index = 0; task_index < NR_TASKS; task_index++) {
            Task* task = tasks[task_index];

            if (!task || task->process != process) {
                continue;
            }

            visited_tasks[task_index] = true;
            attached_task_count++;
            process_has_tasks = true;
            process_dump_task_struct(task);
            shell_show_task_library_locals(task, libraries, library_count);
        }

        if (!process_has_tasks) {
            shell_print_kv("tasks", "%s", "<none attached>");
        }
    }

    shell_print_heading("Orphan Tasks");
    shell_print_kv("attached", "%u", attached_task_count);
    shell_print_kv("scanned processes", "%u", process_count);

    for (int task_index = 0; task_index < NR_TASKS; task_index++) {
        Task* task = tasks[task_index];

        if (!task || visited_tasks[task_index]) {
            continue;
        }

        process_dump_task_struct(task);
        shell_show_task_library_locals(task, libraries, library_count);
    }
}

static Bool shell_require_filesystem(const char* command_name) {
    if (filesystem_is_ready() && vfs_has_root_filesystem()) {
        return true;
    }

    shell_print_error("Filesystem unavailable for %s: no mounted block-backed root filesystem", command_name);
    return false;
}

static void shell_list_directory(const char* path) {
    struct FileDesc dir;
    struct DirectoryEntry entry;
    char size_text[32];
    unsigned long count = 0;

    if (!shell_require_filesystem("ls")) {
        return;
    }

    if (vfs_dir_open(&dir, path) != SUCCESS) {
        shell_print_error("Directory not found: %s", path);
        return;
    }

    shell_print_heading("Directory Listing");
    shell_print_kv("path", SHELL_COLOR_PATH "%s" SHELL_COLOR_RESET, path);
    kprint(SHELL_COLOR_MUTED "  ");
    shell_print_text_column("TYPE", SHELL_LS_TYPE_WIDTH, null);
    kprint(" ");
    shell_print_text_column("SIZE", SHELL_LS_SIZE_WIDTH, null);
    kprint(" NAME" SHELL_COLOR_RESET "\r\n");
    shell_print_separator();
    for (;;) {
        UByte* name_utf8 = null;
        int next = vfs_dir_read_ex(&dir, &entry);

        if (next <= 0) {
            break;
        }
        if (str_from_utf16(entry.long_name, 255, &name_utf8) == 0) {
            const char* kind = (entry.attr & 0x10) ? "dir" : "file";
            const char* kind_color = (entry.attr & 0x10) ? SHELL_COLOR_OK : SHELL_COLOR_LABEL;

            kprint("  ");
            shell_print_text_column(kind, SHELL_LS_TYPE_WIDTH, kind_color);
            kprint(" ");
            shell_format_text(size_text, "%d", entry.size);
            shell_print_text_column(size_text, SHELL_LS_SIZE_WIDTH, null);
            kprint(" %s\r\n", name_utf8);
            kfree((Address)name_utf8);
            count++;
        }
    }

    if (count == 0) {
        kprint("  (empty)\r\n");
    }

    vfs_dir_close(&dir);
}

static void shell_cat_file(const char* path) {
    struct FileDesc fd;
    char buffer[129];

    if (!shell_require_filesystem("cat")) {
        return;
    }

    if (vfs_fd_open(&fd, path, O_READ) != SUCCESS) {
        shell_print_error("File not found: %s", path);
        return;
    }

    shell_print_heading("File View");
    shell_print_kv("path", SHELL_COLOR_PATH "%s" SHELL_COLOR_RESET, path);

    for (;;) {
        int read = vfs_fd_read(&fd, buffer, sizeof(buffer) - 1);

        if (read < 0) {
            shell_print_error("Read error: %s", path);
            break;
        }
        if (read == 0) {
            break;
        }

        buffer[read] = '\0';
        kprint("%s", buffer);
    }

    kprint("\r\n");
    vfs_fd_close(&fd);
}

static void shell_make_default_name(const char* path, char* name, int size) {
    const char* base = path;
    int index = 0;

    while (*path != '\0') {
        if (*path == '/') {
            base = path + 1;
        }
        path++;
    }

    while (base[index] != '\0' && base[index] != '.' && index < size - 1) {
        name[index] = base[index];
        index++;
    }
    name[index] = '\0';

    if (name[0] == '\0') {
        strncpy(name, "app", size - 1);
        name[size - 1] = '\0';
    }
}

static Bool shell_path_contains_directory(const char* path) {
    if (!path) {
        return false;
    }

    while (*path != '\0') {
        if (*path == '/') {
            return true;
        }
        path++;
    }

    return false;
}

static Bool shell_resolve_program_path(const char* requested_path, char* resolved_path, int size) {
    struct FileDesc fd;

    if (!requested_path || !resolved_path || size <= 0) {
        return false;
    }

    if (vfs_fd_open(&fd, requested_path, O_READ) == SUCCESS) {
        strncpy(resolved_path, requested_path, (unsigned int)size - 1U);
        resolved_path[size - 1] = '\0';
        vfs_fd_close(&fd);
        return true;
    }

    if (!shell_path_contains_directory(requested_path)) {
        char candidate[SHELL_PATH_MAX];

        shell_format_text(candidate, "/bin/%s", requested_path);
        if (vfs_fd_open(&fd, candidate, O_READ) == SUCCESS) {
            strncpy(resolved_path, candidate, (unsigned int)size - 1U);
            resolved_path[size - 1] = '\0';
            vfs_fd_close(&fd);
            return true;
        }

        shell_format_text(candidate, "/bin/%s.exe", requested_path);
        if (vfs_fd_open(&fd, candidate, O_READ) == SUCCESS) {
            strncpy(resolved_path, candidate, (unsigned int)size - 1U);
            resolved_path[size - 1] = '\0';
            vfs_fd_close(&fd);
            return true;
        }
    }

    return false;
}

static void shell_run_program(const char* path, const char* requested_name) {
    char resolved_path[SHELL_PATH_MAX];
    char name[SHELL_NAME_MAX];
    int pid;

    if (!shell_require_filesystem("run")) {
        return;
    }

    if (!shell_resolve_program_path(path, resolved_path, sizeof(resolved_path))) {
        shell_print_error("Program not found: %s", path);
        return;
    }

    if (requested_name && requested_name[0] != '\0') {
        strncpy(name, requested_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    else {
        shell_make_default_name(resolved_path, name, sizeof(name));
    }

    pid = spawn_user_program(resolved_path, name, 0);
    if (pid < 0) {
        shell_print_error("Failed to spawn %s", resolved_path);
        return;
    }

    shell_print_heading("Spawned Program");
    shell_print_kv("path", SHELL_COLOR_PATH "%s" SHELL_COLOR_RESET, resolved_path);
    shell_print_kv("task", "%d", pid);
    shell_print_kv("name", "%s", name);
}

static void shell_execute(char* line) {
    char* cursor = line;
    char* command = shell_next_token(&cursor);

    if (!command) {
        shell_print_help();
        return;
    }
    if (strcmp(command, "help") == 0) {
        shell_print_help();
        return;
    }
    if (strcmp(command, "exit") == 0) {
        shell_print_heading("Shutdown");
        shell_print_kv("mode", "halting board");
        device_shutdown();
        return;
    }
    if (strcmp(command, "cpu") == 0) {
        char* cpu = shell_next_token(&cursor);

        if (cpu) {
            shell_show_cpu_detail(cpu);
        }
        else {
            shell_show_cpu_info();
        }
        return;
    }
    if (strcmp(command, "sched") == 0) {
        shell_show_scheduler_info();
        return;
    }
    if (strcmp(command, "usage") == 0) {
        unsigned long samples;
        unsigned long delay_ms;

        if (shell_parse_monitor_args(&cursor, "usage [samples] [delay_ms]", &samples, &delay_ms) != 0) {
            return;
        }
        shell_run_monitor(shell_show_usage_snapshot, samples, delay_ms);
        return;
    }
    if (strcmp(command, "top") == 0) {
        unsigned long samples;
        unsigned long delay_ms;

        if (shell_parse_monitor_args(&cursor, "top [samples] [delay_ms]", &samples, &delay_ms) != 0) {
            return;
        }
        shell_run_monitor(shell_show_top_snapshot, samples, delay_ms);
        return;
    }
    if (strcmp(command, "mpstat") == 0) {
        unsigned long samples;
        unsigned long delay_ms;

        if (shell_parse_monitor_args(&cursor, "mpstat [samples] [delay_ms]", &samples, &delay_ms) != 0) {
            return;
        }
        shell_run_monitor(shell_show_mpstat_snapshot, samples, delay_ms);
        return;
    }
    if (strcmp(command, "hwinfo") == 0) {
        shell_show_hardware_info();
        return;
    }
    if (strcmp(command, "lsdev") == 0) {
        shell_show_devices();
        return;
    }
    if (strcmp(command, "ls") == 0) {
        char* path = shell_next_token(&cursor);
        shell_list_directory(path ? path : "/");
        return;
    }
    if (strcmp(command, "cat") == 0) {
        char* path = shell_next_token(&cursor);

        if (!path) {
            shell_print_usage("cat <path>");
            return;
        }

        shell_cat_file(path);
        return;
    }
    if (strcmp(command, "run") == 0) {
        char* path = shell_next_token(&cursor);
        char* name = shell_next_token(&cursor);

        if (!path) {
            shell_print_usage("run <path> [name]");
            return;
        }

        shell_run_program(path, name);
        return;
    }
    if (strcmp(command, "modules") == 0) {
        shell_show_modules(shell_next_token(&cursor));
        return;
    }
    if (strcmp(command, "libs") == 0) {
        shell_show_shared_libraries();
        return;
    }
    if (strcmp(command, "ps") == 0) {
        shell_print_task_table();
        return;
    }
    if (strcmp(command, "ptree") == 0) {
        shell_show_process_tree();
        return;
    }
    if (strcmp(command, "inspect") == 0) {
        shell_show_system_inspect();
        return;
    }
    if (strcmp(command, "task") == 0) {
        shell_show_task(shell_next_token(&cursor));
        return;
    }
    if (strcmp(command, "maps") == 0) {
        shell_show_maps(shell_next_token(&cursor));
        return;
    }
    if (strcmp(command, "pte") == 0) {
        char* pid = shell_next_token(&cursor);
        char* va = shell_next_token(&cursor);

        shell_show_pte(pid, va);
        return;
    }
    if (strcmp(command, "mem") == 0) {
        char* scope = shell_next_token(&cursor);

        if (!scope) {
            shell_print_usage("mem <addr> [len]");
            return;
        }
        if (strcmp(scope, "phys") == 0) {
            char* addr = shell_next_token(&cursor);
            char* length = shell_next_token(&cursor);

            if (!addr) {
                shell_print_usage("mem phys <pa> [len]");
                return;
            }
            {
                unsigned long parsed_addr;
                unsigned long parsed_length = 128;

                if (shell_parse_ulong(addr, &parsed_addr) != 0) {
                    shell_print_error("Invalid physical address: %s", addr);
                    return;
                }
                if (length && shell_parse_ulong(length, &parsed_length) != 0) {
                    shell_print_usage("mem phys <pa> [len]");
                    return;
                }
                shell_dump_host_memory(parsed_addr, parsed_length, true);
            }
        }
        else if (strcmp(scope, "virt") == 0) {
            char* addr = shell_next_token(&cursor);
            char* length = shell_next_token(&cursor);
            unsigned long parsed_addr;
            unsigned long parsed_length = 128;

            if (!addr) {
                shell_print_usage("mem virt <va> [len]");
                return;
            }
            if (shell_parse_ulong(addr, &parsed_addr) != 0) {
                shell_print_error("Invalid virtual address: %s", addr);
                return;
            }
            if (length && shell_parse_ulong(length, &parsed_length) != 0) {
                shell_print_usage("mem virt <va> [len]");
                return;
            }
            shell_dump_host_memory(parsed_addr, parsed_length, false);
        }
        else if (strcmp(scope, "task") == 0) {
            shell_dump_task_memory(shell_next_token(&cursor), shell_next_token(&cursor), shell_next_token(&cursor));
        }
        else {
            shell_dump_memory(scope, shell_next_token(&cursor));
        }
        return;
    }
    if (strcmp(command, "mwrite") == 0) {
        char* scope = shell_next_token(&cursor);

        if (!scope) {
            shell_print_usage("mwrite <addr> <byte...>");
            return;
        }
        if (strcmp(scope, "phys") == 0) {
            char* addr = shell_next_token(&cursor);
            UByte bytes[SHELL_WRITE_MAX_BYTES];
            unsigned long parsed_addr;
            int count;

            if (!addr || shell_parse_ulong(addr, &parsed_addr) != 0) {
                shell_print_usage("mwrite phys <pa> <byte...>");
                return;
            }
            count = shell_parse_write_bytes(cursor, bytes, SHELL_WRITE_MAX_BYTES);
            if (count <= 0) {
                shell_print_usage("mwrite phys <pa> <byte...>");
                return;
            }
            shell_write_host_memory(parsed_addr, bytes, count, true);
            return;
        }
        if (strcmp(scope, "virt") == 0) {
            char* addr = shell_next_token(&cursor);
            UByte bytes[SHELL_WRITE_MAX_BYTES];
            unsigned long parsed_addr;
            int count;

            if (!addr || shell_parse_ulong(addr, &parsed_addr) != 0) {
                shell_print_usage("mwrite virt <va> <byte...>");
                return;
            }
            count = shell_parse_write_bytes(cursor, bytes, SHELL_WRITE_MAX_BYTES);
            if (count <= 0) {
                shell_print_usage("mwrite virt <va> <byte...>");
                return;
            }
            shell_write_host_memory(parsed_addr, bytes, count, false);
            return;
        }
        if (strcmp(scope, "task") == 0) {
            shell_write_task_memory(shell_next_token(&cursor), shell_next_token(&cursor), cursor);
            return;
        }

        shell_write_memory(scope, cursor);
        return;
    }
    if (strcmp(command, "mmutest") == 0) {
        int result = process_mmu_self_test();

        shell_print_heading("MMU Self-Test");
        kprint("  result         " SHELL_COLOR_MUTED "|" SHELL_COLOR_RESET " %s%s%s\r\n",
            result == 0 ? SHELL_COLOR_OK : SHELL_COLOR_ERROR,
            result == 0 ? "PASS" : "FAIL",
            SHELL_COLOR_RESET);
        return;
    }
    if (strcmp(command, "heaptest") == 0) {
        shell_run_heaptest(shell_next_token(&cursor));
        return;
    }
    if (strcmp(command, "ticks") == 0) {
        shell_print_heading("Scheduler Ticks");
        shell_print_kv("ticks", "%lu", schedler_get_ticks());
        return;
    }
    if (strcmp(command, "clear") == 0) {
        shell_clear_screen();
        return;
    }

    shell_print_error("Unknown command: %s", command);
}

int kernel_shell_execute_command(const char* line) {
    char command_line[SHELL_INPUT_MAX];

    if (!line || line[0] == '\0') {
        return -1;
    }

    strncpy(command_line, line, sizeof(command_line) - 1);
    command_line[sizeof(command_line) - 1] = '\0';
    shell_execute(command_line);
    return 0;
}

static void shell_read_line(char* buffer, int size) {
    int index = 0;

    for (;;) {
        char ch = (char)input_read_key();

        if (ch == '\n') {
            shell_forward_gui_key(ch);
            buffer[index] = '\0';
            kprint("\r\n");
            return;
        }
        if (ch == 8 || ch == 127) {
            if (index > 0) {
                index--;
                kprint("\b \b");
            }
            shell_forward_gui_key(ch);
            continue;
        }
        if (ch < ' ' || index >= size - 1) {
            continue;
        }

        buffer[index++] = ch;
        shell_forward_gui_key(ch);
        kprint("%c", ch);
    }
}

void kernel_shell_main(Pointer arg) {
    char line[SHELL_INPUT_MAX];

    log_info("Starting kernel shell");
    (void)arg;

    if (current_process) {
        strncpy(current_process->name, "shell", sizeof(current_process->name) - 1);
        current_process->name[sizeof(current_process->name) - 1] = '\0';
        current_task->name = (Buffer)current_process->name;
    }
    else {
        strncpy(current_task->thread_name, "shell", sizeof(current_task->thread_name) - 1);
        current_task->thread_name[sizeof(current_task->thread_name) - 1] = '\0';
        current_task->name = (Buffer)current_task->thread_name;
    }

    kprint("%s", shell_banner);
    shell_print_prompt();

    for (;;) {
        shell_read_line(line, sizeof(line));
        shell_execute(line);
        shell_print_prompt();
    }
}
