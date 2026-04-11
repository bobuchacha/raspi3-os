#include "app/kernel.h"
#include "app/kernel_gui.h"
#include "user_runtime.h"
#include "app/core_log.h"

#define SHELL_LINE_MAX 128
#define SHELL_HISTORY_MAX 16
#define SHELL_PATH_MAX 256
#define SHELL_TASK_SCAN_MAX 64
#define SHELL_SEARCH_PATH_MAX 8
#define SHELL_TASK_NAME_MAX 32
#define SHELL_LAUNCH_ARGS_MAX 128
#define SHELL_TOKEN_MAX 16
#define SHELL_LS_TYPE_WIDTH 8
#define SHELL_LS_SIZE_WIDTH 12

static const char* const g_shell_default_search_paths[] = {
    "C:\\bin",
};

typedef struct ShellState {
    char cwd[SHELL_PATH_MAX];
    char history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
    int history_count;
    char search_paths[SHELL_SEARCH_PATH_MAX][SHELL_PATH_MAX];
    int search_path_count;
} ShellState;

static void shell_resolve_path(const char* cwd, const char* input, char* output);
static void shell_default_name(const char* path, char* name, int size);
static void shell_reset_search_paths(ShellState* state);

/*
 * shell_append_long
 *
 * Local signed-number formatter used by the shell output helpers. The newer
 * user-runtime path does not link the old `fprint` utility, so the shell keeps
 * its numeric formatting self-contained and loader-friendly.
 */
static char* shell_append_long(char* dst, long value) {
    if (value < 0) {
        *dst++ = '-';
        return appendUnsignedLong(dst, (unsigned long)(-value));
    }

    return appendUnsignedLong(dst, (unsigned long)value);
}

/*
 * shell_format_cursor_left
 *
 * Build the ANSI escape used by the inline line editor to move the cursor left
 * after rewriting the current input buffer.
 */
static void shell_format_cursor_left(char* buffer, unsigned long count) {
    char* cursor = buffer;

    cursor = appendText(cursor, "\033[");
    cursor = appendUnsignedLong(cursor, count);
    cursor = appendText(cursor, "D");
    *cursor = '\0';
}

/*
 * shell_forward_gui_key
 *
 * Keep shell input on the console path only.
 * Deliberately avoid mirroring typed characters into the framebuffer GUI.
 */
static void shell_forward_gui_key(char ch) {
    (void)ch;
}

static int shell_gui_push_key(unsigned long key) {
    long result = -1;

    if (user_kernel_module_invoke("fbgui", "push_key", key, 0, &result) != 0) {
        return -1;
    }

    return result < 0 ? -1 : 0;
}

static int shell_gui_push_pointer(unsigned long x, unsigned long y) {
    long result = -1;

    if (user_kernel_module_invoke("fbgui", "push_pointer", x, y, &result) != 0) {
        return -1;
    }

    return result < 0 ? -1 : 0;
}

static int shell_gui_write_text(const char* text) {
    while (text && *text != '\0') {
        if (shell_gui_push_key((unsigned long)(unsigned char)*text++) != 0) {
            return -1;
        }
    }

    return 0;
}

static unsigned long shell_strlen(const char* text) {
    unsigned long length = 0;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static int shell_strcmp(const char* lhs, const char* rhs) {
    while (*lhs != '\0' && *lhs == *rhs) {
        lhs++;
        rhs++;
    }
    return (unsigned char)*lhs - (unsigned char)*rhs;
}

/*
 * shell_ascii_lower
 *
 * Keep command matching ASCII-only and libc-free so the shell stays aligned
 * with the rest of the userspace runtime.
 */
static char shell_ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }

    return ch;
}

/*
 * shell_ascii_upper
 *
 * Drive letters are displayed in uppercase to match the Windows-style command
 * shell presentation even though the kernel path translator accepts either
 * case.
 */
static char shell_ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }

    return ch;
}

/*
 * shell_same_text_case_insensitive
 *
 * Built-in commands should feel like a Windows CE shell rather than a Unix
 * prompt, so command verbs and PATH subcommands are matched without case
 * sensitivity.
 */
static int shell_same_text_case_insensitive(const char* lhs, const char* rhs) {
    while (*lhs != '\0' && *rhs != '\0') {
        if (shell_ascii_lower(*lhs) != shell_ascii_lower(*rhs)) {
            return 0;
        }
        lhs++;
        rhs++;
    }

    return *lhs == *rhs;
}

/*
 * shell_is_path_separator
 *
 * The kernel accepts both slash forms, so the shell normalizes either variant
 * while presenting backslashes in user-visible paths.
 */
static int shell_is_path_separator(char ch) {
    return ch == '/' || ch == '\\';
}

/*
 * shell_is_drive_letter
 *
 * Absolute user-visible paths can begin with a drive designator, which keeps
 * the prompt and command syntax consistent with the rest of the Windows-like
 * VFS namespace.
 */
static int shell_is_drive_letter(char ch) {
    ch = shell_ascii_upper(ch);
    return ch >= 'A' && ch <= 'Z';
}

static int shell_path_has_drive_prefix(const char* path) {
    return path && shell_is_drive_letter(path[0]) && path[1] == ':';
}

static void shell_set_root_path(char drive_letter, char* path) {
    path[0] = shell_ascii_upper(drive_letter);
    path[1] = ':';
    path[2] = '\\';
    path[3] = '\0';
}

static void shell_strcpy(char* dest, const char* src) {
    while (*src != '\0') {
        *dest++ = *src++;
    }
    *dest = '\0';
}

static void shell_strncpy(char* dest, const char* src, int size) {
    int index = 0;

    if (size <= 0) {
        return;
    }
    while (index < size - 1 && src[index] != '\0') {
        dest[index] = src[index];
        index++;
    }
    dest[index] = '\0';
}

static void shell_strcat(char* dest, const char* src) {
    shell_strcpy(dest + shell_strlen(dest), src);
}

static void shell_memmove(char* dest, const char* src, int size) {
    int index;

    if (size <= 0 || dest == src) {
        return;
    }
    if (dest < src) {
        for (index = 0; index < size; index++) {
            dest[index] = src[index];
        }
        return;
    }
    for (index = size - 1; index >= 0; index--) {
        dest[index] = src[index];
    }
}

static void shell_bzero(void* ptr, unsigned long size) {
    unsigned char* bytes = (unsigned char*)ptr;

    while (size > 0) {
        *bytes++ = 0;
        size--;
    }
}

static void shell_copy_line(char* dest, const char* src, int size) {
    int index = 0;

    if (size <= 0) {
        return;
    }
    while (index < size - 1 && src[index] != '\0') {
        dest[index] = src[index];
        index++;
    }
    dest[index] = '\0';
}

static void shell_write(const char* text) {
    char buffer[256];
    int index = 0;

    if (!text) {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            if (index >= (int)sizeof(buffer) - 2) {
                buffer[index] = '\0';
                if (writeText(buffer) < 0) {
                    return;
                }
                index = 0;
            }
            buffer[index++] = '\r';
        }

        if (index >= (int)sizeof(buffer) - 1) {
            buffer[index] = '\0';
            if (writeText(buffer) < 0) {
                return;
            }
            index = 0;
        }

        buffer[index++] = *text++;
    }

    if (index > 0) {
        // writeText expects a null-terminated string, so flush the final
        // partial chunk explicitly before handing the buffer to the syscall.
        // Without this terminator the console walks into stale stack bytes and
        // prints random tail garbage after otherwise valid shell output.
        buffer[index] = '\0';
        if (writeText(buffer) < 0) {
            return;
        }
    }
}

static void shell_write_line(const char* text) {
    shell_write(text);
    shell_write("\n");
}

static void shell_write_spaces(int count) {
    while (count-- > 0) {
        shell_write(" ");
    }
}

static void shell_write_repeat(char ch, int count) {
    char buffer[32];

    while (count > 0) {
        int chunk = count;
        int index;

        if (chunk > (int)sizeof(buffer) - 1) {
            chunk = (int)sizeof(buffer) - 1;
        }
        for (index = 0; index < chunk; index++) {
            buffer[index] = ch;
        }
        buffer[chunk] = '\0';
        shell_write(buffer);
        count -= chunk;
    }
}

static void shell_write_left(const char* text, int width) {
    int length = (int)shell_strlen(text);

    shell_write(text);
    if (length < width) {
        shell_write_spaces(width - length);
    }
}

static void shell_write_right(const char* text, int width) {
    int length = (int)shell_strlen(text);

    if (length < width) {
        shell_write_spaces(width - length);
    }
    shell_write(text);
}

static void shell_write_ulong(unsigned long value, int width) {
    char buffer[32];
    char* cursor = buffer;

    cursor = appendUnsignedLong(cursor, value);
    *cursor = '\0';
    shell_write_right(buffer, width);
}

static void shell_write_long(long value, int width) {
    char buffer[32];
    char* cursor = buffer;

    cursor = shell_append_long(cursor, value);
    *cursor = '\0';
    shell_write_right(buffer, width);
}

static void shell_print_heading(const char* title) {
    int length = (int)shell_strlen(title);

    shell_write_line(title);
    shell_write_repeat('=', length);
    shell_write("\n");
}

static void shell_print_key_value(const char* key, const char* value) {
    shell_write("  ");
    shell_write_left(key, 12);
    shell_write(" : ");
    shell_write_line(value);
}

static void shell_print_number_value(const char* key, unsigned long value, const char* unit) {
    char buffer[64];
    char* cursor = buffer;

    cursor = appendUnsignedLong(cursor, value);
    if (unit && unit[0] != '\0') {
        cursor = appendText(cursor, " ");
        cursor = appendText(cursor, unit);
    }
    *cursor = '\0';
    shell_print_key_value(key, buffer);
}

static void shell_print_error(const char* command, const char* message) {
    shell_write(command);
    shell_write(": ");
    shell_write_line(message);
}

static void shell_print_usage(const char* usage) {
    shell_write("syntax: ");
    shell_write_line(usage);
}

static void shell_print_command_help(const char* command, const char* description) {
    shell_write("  ");
    shell_write_left(command, 20);
    shell_write_line(description);
}

static int shell_is_space(char ch) {
    return ch == ' ' || ch == '\t';
}

static char* shell_skip_spaces(char* text) {
    while (*text != '\0' && shell_is_space(*text)) {
        text++;
    }
    return text;
}

static char* shell_next_token(char** cursor) {
    char* start = shell_skip_spaces(*cursor);
    char* end = start;

    if (*start == '\0') {
        *cursor = start;
        return 0;
    }

    while (*end != '\0' && !shell_is_space(*end)) {
        end++;
    }
    if (*end != '\0') {
        *end = '\0';
        end++;
    }

    *cursor = end;
    return start;
}

static int shell_tokenize(char* text, char* tokens[], int max_tokens) {
    int count = 0;
    char* cursor = text;

    while (count < max_tokens) {
        char* token = shell_next_token(&cursor);

        if (!token) {
            break;
        }
        tokens[count++] = token;
    }

    return count;
}

static void shell_join_tokens(char* output, int size, char* tokens[], int start, int count) {
    int index;

    if (size <= 0) {
        return;
    }

    output[0] = '\0';
    for (index = start; index < count; index++) {
        if (output[0] != '\0' && shell_strlen(output) + 1 < (unsigned long)size) {
            shell_strcat(output, " ");
        }
        if (shell_strlen(output) + shell_strlen(tokens[index]) >= (unsigned long)size) {
            break;
        }
        shell_strcat(output, tokens[index]);
    }
}

static long shell_parse_long(const char* text, int* ok) {
    long value = 0;
    int sign = 1;

    *ok = 0;
    if (!text || *text == '\0') {
        return 0;
    }
    if (*text == '-') {
        sign = -1;
        text++;
    }
    if (*text == '\0') {
        return 0;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        value = (value * 10) + (*text - '0');
        text++;
    }
    *ok = 1;
    return value * sign;
}

static void shell_build_prompt(const ShellState* state, char* prompt) {
    char* cursor = prompt;

    cursor = appendText(cursor, state->cwd);
    cursor = appendText(cursor, "> ");
    *cursor = '\0';
}

static void shell_add_history(ShellState* state, const char* line) {
    int index;

    if (!line || line[0] == '\0') {
        return;
    }
    if (state->history_count > 0 && shell_strcmp(state->history[state->history_count - 1], line) == 0) {
        return;
    }
    if (state->history_count == SHELL_HISTORY_MAX) {
        for (index = 1; index < SHELL_HISTORY_MAX; index++) {
            shell_strcpy(state->history[index - 1], state->history[index]);
        }
        state->history_count--;
    }

    shell_strcpy(state->history[state->history_count++], line);
}

static void shell_replace_line(char* line, int* length, int* cursor, const char* value, int size) {
    shell_strncpy(line, value, size);
    line[size - 1] = '\0';
    *length = (int)shell_strlen(line);
    *cursor = *length;
}

static void shell_redraw_line(const char* prompt, const char* line, int length, int cursor) {
    char buffer[32];

    shell_write("\r");
    shell_write(prompt);
    shell_write(line);
    shell_write("\033[K");
    if (length > cursor) {
        shell_format_cursor_left(buffer, (unsigned long)(length - cursor));
        shell_write(buffer);
    }
}

static void shell_write_char(char ch) {
    char buffer[2];

    buffer[0] = ch;
    buffer[1] = '\0';
    shell_write(buffer);
}

static void shell_erase_tail_char(void) {
    shell_write("\b \b");
}

static int shell_read_char(void) {
    return (int)readConsole();
}

static void shell_read_line(ShellState* state, char* line, int size) {
    char prompt[320];
    char draft[SHELL_LINE_MAX];
    int history_index = -1;
    int length = 0;
    int cursor = 0;

    draft[0] = '\0';
    line[0] = '\0';
    shell_build_prompt(state, prompt);
    shell_write(prompt);

    for (;;) {
        int raw = shell_read_char();
        char ch = (char)raw;

        if (raw < 0) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            line[length] = '\0';
            shell_forward_gui_key('\n');
            shell_write("\r\n");
            return;
        }
        if (ch == 27) {
            int next = shell_read_char();
            if (next == '[') {
                int code = shell_read_char();
                if (code == 'A') {
                    if (state->history_count == 0) {
                        continue;
                    }
                    if (history_index < 0) {
                        shell_strcpy(draft, line);
                        history_index = state->history_count - 1;
                    }
                    else if (history_index > 0) {
                        history_index--;
                    }
                    shell_replace_line(line, &length, &cursor, state->history[history_index], size);
                    shell_redraw_line(prompt, line, length, cursor);
                    continue;
                }
                if (code == 'B') {
                    if (history_index < 0) {
                        continue;
                    }
                    history_index++;
                    if (history_index >= state->history_count) {
                        history_index = -1;
                        shell_replace_line(line, &length, &cursor, draft, size);
                    }
                    else {
                        shell_replace_line(line, &length, &cursor, state->history[history_index], size);
                    }
                    shell_redraw_line(prompt, line, length, cursor);
                    continue;
                }
                if (code == 'C') {
                    if (cursor < length) {
                        cursor++;
                        shell_write("\033[C");
                    }
                    continue;
                }
                if (code == 'D') {
                    if (cursor > 0) {
                        cursor--;
                        shell_write("\033[D");
                    }
                    continue;
                }
            }
            continue;
        }
        if (ch == 8 || ch == 127) {
            if (cursor > 0) {
                shell_memmove(line + cursor - 1, line + cursor, length - cursor + 1);
                cursor--;
                length--;
                shell_forward_gui_key(8);
                if (cursor == length) {
                    shell_erase_tail_char();
                }
                else {
                    shell_redraw_line(prompt, line, length, cursor);
                }
            }
            continue;
        }
        if (ch < ' ' || length >= size - 1) {
            continue;
        }

        shell_memmove(line + cursor + 1, line + cursor, length - cursor + 1);
        line[cursor] = ch;
        cursor++;
        length++;
        shell_forward_gui_key(ch);
        if (cursor == length) {
            shell_write_char(ch);
        }
        else {
            shell_redraw_line(prompt, line, length, cursor);
        }
    }
}

static void shell_pop_path(char* path) {
    int length = (int)shell_strlen(path);

    if (length <= 3) {
        shell_set_root_path(shell_path_has_drive_prefix(path) ? path[0] : 'C', path);
        return;
    }

    while (length > 3 && shell_is_path_separator(path[length - 1])) {
        length--;
    }

    while (length > 3 && !shell_is_path_separator(path[length - 1])) {
        length--;
    }

    if (length > 3) {
        path[length - 1] = '\0';
    }
    else {
        path[3] = '\0';
    }
}

static void shell_append_segment(char* path, const char* segment) {
    int length = (int)shell_strlen(path);

    if (length > 0 && !shell_is_path_separator(path[length - 1])) {
        shell_strcat(path, "\\");
    }
    shell_strcat(path, segment);
}

static int shell_path_has_extension(const char* path) {
    const char* base = path;

    while (*path != '\0') {
        if (shell_is_path_separator(*path)) {
            base = path + 1;
        }
        path++;
    }

    while (*base != '\0') {
        if (*base == '.') {
            return 1;
        }
        base++;
    }

    return 0;
}

static int shell_path_contains_separator(const char* path) {
    while (path && *path != '\0') {
        if (shell_is_path_separator(*path)) {
            return 1;
        }
        path++;
    }
    return 0;
}

static int shell_spawn_program(const char* path, const char* name_arg, const char* args_arg) {
    char name[32];
    char buffer[256];
    long pid;

    if (name_arg && name_arg[0] != '\0') {
        shell_strncpy(name, name_arg, sizeof(name));
        name[sizeof(name) - 1] = '\0';
    }
    else {
        shell_default_name(path, name, sizeof(name));
    }

    pid = spawnTask(path, name, args_arg);
    if (pid < 0) {
        return (int)pid;
    }

    shell_print_heading("Spawned task");
    {
        char* cursor = buffer;

        cursor = shell_append_long(cursor, pid);
        *cursor = '\0';
    }
    shell_print_key_value("pid", buffer);
    shell_print_key_value("name", name);
    shell_print_key_value("path", path);
    if (args_arg && args_arg[0] != '\0') {
        shell_print_key_value("args", args_arg);
    }

    return (int)pid;
}

static int shell_spawn_program_nowait(const char* path, const char* name_arg, const char* args_arg) {
    char name[32];
    char buffer[256];
    long pid;

    if (name_arg && name_arg[0] != '\0') {
        shell_strncpy(name, name_arg, sizeof(name));
        name[sizeof(name) - 1] = '\0';
    }
    else {
        shell_default_name(path, name, sizeof(name));
    }

    pid = spawnTask(path, name, args_arg);
    if (pid < 0) {
        return (int)pid;
    }

    shell_print_heading("Spawned task");
    {
        char* cursor = buffer;

        cursor = shell_append_long(cursor, pid);
        *cursor = '\0';
    }
    shell_print_key_value("pid", buffer);
    shell_print_key_value("name", name);
    shell_print_key_value("path", path);
    if (args_arg && args_arg[0] != '\0') {
        shell_print_key_value("args", args_arg);
    }

    return (int)pid;
}

static void shell_print_exit_status(long pid, long exit_code) {
    char line[256];
    char* cursor = line;

    cursor = shell_append_long(cursor, pid);
    cursor = appendText(cursor, " exited with code ");
    cursor = shell_append_long(cursor, exit_code);
    cursor = appendText(cursor, " (0x");
    cursor = appendHex(cursor, (unsigned long)exit_code);
    cursor = appendText(cursor, ")");
    *cursor = '\0';

    shell_print_heading("Task exit");
    shell_print_key_value("status", line);
}

static void shell_wait_for_program_exit(long pid) {
    long exit_code = -1;
    long status = waitPid(pid, &exit_code);

    if (status == -4) {
        shell_print_heading("Task wait");
        {
            char buffer[256];

            snprintf(buffer, sizeof(buffer), "waitPid is not available yet; pid %ld continues in background", pid);
            shell_print_key_value("status", buffer);
        }
        return;
    }

    // Syscalls in this userspace ABI report success as 0 and failures as negative status codes.
    if (status < 0) {
        shell_print_heading("Task exit");
        {
            char buffer[256];

            snprintf(buffer, sizeof(buffer), "wait for pid %ld failed. Status code: %ld", pid, status);
            shell_print_key_value("status", buffer);
        }
        return;
    }

    shell_print_exit_status(pid, exit_code);
}

static int shell_file_exists(const char* path) {
    char probe;
    long status;

    if (!path || path[0] == '\0') {
        return 0;
    }

    // Probe the candidate path before spawning so invalid commands do not create transient tasks.
    status = readFile(path, 0, &probe, 1);
    return status >= 0;
}

static int shell_try_spawn_candidate(const char* path, const char* name_arg, const char* args_arg) {
    int pid;

    if (!shell_file_exists(path)) {
        return 0;
    }

    // Spawn only after the candidate path is confirmed to be a readable file.
    pid = shell_spawn_program(path, name_arg, args_arg);
    if (pid < 0) {
        return pid;
    }

    shell_wait_for_program_exit((long)pid);
    return 1;
}

/*
 * shell_try_spawn_candidate_nowait
 *
 * Long-running monitors should launch through the same validation path as any
 * other executable, but the shell must hand control back to the prompt instead
 * of immediately waiting on the child.
 */
static int shell_try_spawn_candidate_nowait(const char* path, const char* name_arg, const char* args_arg) {
    int pid;

    if (!shell_file_exists(path)) {
        return 0;
    }

    pid = shell_spawn_program_nowait(path, name_arg, args_arg);
    if (pid < 0) {
        return pid;
    }

    shell_write_line("task is running in the background");
    return 1;
}

static int shell_search_path_index(const ShellState* state, const char* path) {
    int index;

    for (index = 0; index < state->search_path_count; index++) {
        if (shell_same_text_case_insensitive(state->search_paths[index], path)) {
            return index;
        }
    }

    return -1;
}

static int shell_add_search_path(ShellState* state, const char* path) {
    char resolved[SHELL_PATH_MAX];

    if (!path || path[0] == '\0') {
        return -1;
    }

    shell_resolve_path(state->cwd, path, resolved);
    if (shell_search_path_index(state, resolved) >= 0) {
        return 0;
    }
    if (state->search_path_count >= SHELL_SEARCH_PATH_MAX) {
        return -1;
    }

    shell_strncpy(state->search_paths[state->search_path_count], resolved, sizeof(state->search_paths[state->search_path_count]));
    state->search_paths[state->search_path_count][sizeof(state->search_paths[state->search_path_count]) - 1] = '\0';
    state->search_path_count++;
    return 0;
}

/*
 * shell_task_state_name
 *
 * The task-info syscall now reports the main-thread scheduler state so `ps`
 * can distinguish runnable tasks from sleepers and other blocked waits. That
 * keeps the command aligned with what the scheduler is actually doing instead
 * of the coarser process lifecycle state.
 */
static const char* shell_task_state_name(long state) {
    if (state == USER_TASK_STATE_INITIALIZED) {
        return "INIT";
    }
    if (state == USER_TASK_STATE_READY) {
        return "READY";
    }
    if (state == USER_TASK_STATE_RUNNING) {
        return "RUN";
    }
    if (state == USER_TASK_STATE_WAITING) {
        return "WAIT";
    }
    if (state == USER_TASK_STATE_SUSPENDED) {
        return "SUSP";
    }
    if (state == USER_TASK_STATE_TERMINATED) {
        return "TERM";
    }

    return "?";
}

/*
 * shell_task_wait_reason_name
 *
 * A dedicated wait column keeps `ps` honest about why a task is parked. That
 * is more actionable than the old COUNT column, which looked like per-task CPU
 * time even though the kernel only exposed a global scheduler tick snapshot.
 *
 * @param wait_reason Wait-reason code mirrored from the task-info syscall.
 * @return Short uppercase label suitable for the fixed-width task table.
 */
static const char* shell_task_wait_reason_name(long wait_reason) {
    if (wait_reason == USER_TASK_WAIT_NONE) {
        return "-";
    }
    if (wait_reason == USER_TASK_WAIT_DELAY) {
        return "SLEEP";
    }
    if (wait_reason == USER_TASK_WAIT_EVENT) {
        return "EVENT";
    }
    if (wait_reason == USER_TASK_WAIT_MUTEX) {
        return "MUTEX";
    }
    if (wait_reason == USER_TASK_WAIT_SEMAPHORE) {
        return "SEMA";
    }
    if (wait_reason == USER_TASK_WAIT_MESSAGE) {
        return "MSG";
    }
    if (wait_reason == USER_TASK_WAIT_IO) {
        return "IO";
    }

    return "?";
}

static void shell_reset_search_paths(ShellState* state) {
    unsigned long index;

    state->search_path_count = 0;
    for (index = 0; index < sizeof(g_shell_default_search_paths) / sizeof(g_shell_default_search_paths[0]); index++) {
        shell_add_search_path(state, g_shell_default_search_paths[index]);
    }
}

static void shell_print_search_paths(const ShellState* state) {
    int index;

    shell_print_heading("Search paths");
    if (state->search_path_count == 0) {
        shell_write_line("  (empty)");
        return;
    }
    for (index = 0; index < state->search_path_count; index++) {
        shell_write("  ");
        shell_write_line(state->search_paths[index]);
    }
}

static int shell_parse_launch_options(char* tokens[], int token_count, int* next_index, const char** name_arg, int* used_separator) {
    int index = 0;

    *name_arg = 0;
    *used_separator = 0;
    while (index < token_count) {
        if (shell_same_text_case_insensitive(tokens[index], "--name")) {
            if (index + 1 >= token_count || tokens[index + 1][0] == '\0') {
                return -1;
            }
            *name_arg = tokens[index + 1];
            index += 2;
            continue;
        }
        if (shell_strcmp(tokens[index], "--") == 0) {
            *used_separator = 1;
            index++;
        }
        break;
    }

    *next_index = index;
    return 0;
}

static int shell_try_spawn_target(ShellState* state, const char* input, const char* name_arg, const char* args_arg) {
    char candidate[SHELL_PATH_MAX];
    char with_ext[SHELL_PATH_MAX];
    int result;
    int index;

    if (!input || input[0] == '\0') {
        return 1;
    }

    if (shell_path_has_drive_prefix(input) || shell_path_contains_separator(input) || input[0] == '/' || input[0] == '\\') {
        shell_resolve_path(state->cwd, input, candidate);
        if (!shell_path_has_extension(candidate) && shell_strlen(candidate) + 4 < sizeof(with_ext)) {
            shell_strncpy(with_ext, candidate, sizeof(with_ext));
            with_ext[sizeof(with_ext) - 1] = '\0';
            shell_strcat(with_ext, ".exe");
            result = shell_try_spawn_candidate(with_ext, name_arg, args_arg);
            if (result > 0) {
                return 0;
            }
            if (result < 0) {
                return result;
            }
        }
        result = shell_try_spawn_candidate(candidate, name_arg, args_arg);
        if (result > 0) {
            return 0;
        }
        return result < 0 ? result : 1;
    }

    for (index = 0; index < state->search_path_count; index++) {
        shell_strncpy(candidate, state->search_paths[index], sizeof(candidate));
        candidate[sizeof(candidate) - 1] = '\0';
        shell_append_segment(candidate, input);
        if (!shell_path_has_extension(candidate) && shell_strlen(candidate) + 4 < sizeof(with_ext)) {
            shell_strncpy(with_ext, candidate, sizeof(with_ext));
            with_ext[sizeof(with_ext) - 1] = '\0';
            shell_strcat(with_ext, ".exe");
            result = shell_try_spawn_candidate(with_ext, name_arg, args_arg);
            if (result > 0) {
                return 0;
            }
            if (result < 0) {
                return result;
            }
        }
        result = shell_try_spawn_candidate(candidate, name_arg, args_arg);
        if (result > 0) {
            return 0;
        }
        if (result < 0) {
            return result;
        }
    }

    return 1;
}

/*
 * shell_try_spawn_target_nowait
 *
 * Background launches must honor the normal executable search rules so `start
 * kevent 8` behaves the same as `kevent 8`, minus the foreground wait.
 */
static int shell_try_spawn_target_nowait(ShellState* state, const char* input, const char* name_arg, const char* args_arg) {
    char candidate[SHELL_PATH_MAX];
    char with_ext[SHELL_PATH_MAX];
    int result;
    int index;

    if (!input || input[0] == '\0') {
        return 1;
    }

    if (shell_path_has_drive_prefix(input) || shell_path_contains_separator(input) || input[0] == '/' || input[0] == '\\') {
        shell_resolve_path(state->cwd, input, candidate);
        if (!shell_path_has_extension(candidate) && shell_strlen(candidate) + 4 < sizeof(with_ext)) {
            shell_strncpy(with_ext, candidate, sizeof(with_ext));
            with_ext[sizeof(with_ext) - 1] = '\0';
            shell_strcat(with_ext, ".exe");
            result = shell_try_spawn_candidate_nowait(with_ext, name_arg, args_arg);
            if (result > 0) {
                return 0;
            }
            if (result < 0) {
                return result;
            }
        }
        result = shell_try_spawn_candidate_nowait(candidate, name_arg, args_arg);
        if (result > 0) {
            return 0;
        }
        return result < 0 ? result : 1;
    }

    for (index = 0; index < state->search_path_count; index++) {
        shell_strncpy(candidate, state->search_paths[index], sizeof(candidate));
        candidate[sizeof(candidate) - 1] = '\0';
        shell_append_segment(candidate, input);
        if (!shell_path_has_extension(candidate) && shell_strlen(candidate) + 4 < sizeof(with_ext)) {
            shell_strncpy(with_ext, candidate, sizeof(with_ext));
            with_ext[sizeof(with_ext) - 1] = '\0';
            shell_strcat(with_ext, ".exe");
            result = shell_try_spawn_candidate_nowait(with_ext, name_arg, args_arg);
            if (result > 0) {
                return 0;
            }
            if (result < 0) {
                return result;
            }
        }
        result = shell_try_spawn_candidate_nowait(candidate, name_arg, args_arg);
        if (result > 0) {
            return 0;
        }
        if (result < 0) {
            return result;
        }
    }

    return 1;
}

static void shell_resolve_path(const char* cwd, const char* input, char* output) {
    char token[128];
    int token_length = 0;
    char drive_letter = 'C';

    if (!input || input[0] == '\0') {
        shell_strncpy(output, cwd, SHELL_PATH_MAX);
        output[SHELL_PATH_MAX - 1] = '\0';
        return;
    }

    if (cwd && shell_path_has_drive_prefix(cwd)) {
        drive_letter = cwd[0];
    }

    if (shell_path_has_drive_prefix(input)) {
        drive_letter = input[0];
        shell_set_root_path(drive_letter, output);
        input += 2;
        while (shell_is_path_separator(*input)) {
            input++;
        }
    }
    else if (shell_is_path_separator(input[0])) {
        shell_set_root_path(drive_letter, output);
        while (shell_is_path_separator(*input)) {
            input++;
        }
    }
    else {
        shell_strncpy(output, cwd, SHELL_PATH_MAX);
        output[SHELL_PATH_MAX - 1] = '\0';
    }

    for (;;) {
        if (shell_is_path_separator(*input) || *input == '\0') {
            token[token_length] = '\0';
            if (token_length > 0) {
                if (shell_strcmp(token, ".") == 0) {
                }
                else if (shell_strcmp(token, "..") == 0) {
                    shell_pop_path(output);
                }
                else if ((int)(shell_strlen(output) + token_length + 2) < SHELL_PATH_MAX) {
                    shell_append_segment(output, token);
                }
            }
            token_length = 0;
            if (*input == '\0') {
                break;
            }
            while (shell_is_path_separator(*input)) {
                input++;
            }
            continue;
        }
        if (token_length < (int)sizeof(token) - 1) {
            token[token_length++] = *input;
        }
        input++;
    }

    if (output[0] == '\0') {
        shell_set_root_path(drive_letter, output);
    }
}

static void shell_print_help(void) {
    shell_print_heading("Shell Commands");
    shell_write_line("Primary commands:");
    shell_print_command_help("help", "Show this help");
    shell_print_command_help("dir [path]", "List files and directories");
    shell_print_command_help("cd [path]", "Show or change the current directory");
    shell_print_command_help("md <path>", "Create a directory");
    shell_print_command_help("del <path>", "Delete a file or empty directory");
    shell_print_command_help("rd <path>", "Remove an empty directory");
    shell_print_command_help("type <path>", "Display a text file");
    shell_print_command_help("cls", "Clear the screen");
    shell_print_command_help("path [subcommand]", "Show or change executable search paths");
    shell_print_command_help("run [--name n] <path> [-- args]", "Spawn a user program");
    shell_print_command_help("start [--name n] <cmd> [-- args]", "Spawn a program in the background");
    shell_print_command_help("ps", "List tasks");
    shell_print_command_help("mem", "Show memory usage");
    shell_print_command_help("guidemo", "Demonstrate framebuffer keyboard and pointer input");
    shell_print_command_help("kdebug <command>", "Run one kernel debug-shell command");
    shell_print_command_help("ipc-test", "Run the IPC broker demo");
    shell_print_command_help("kill <pid>", "Terminate a task");
    shell_print_command_help("reboot", "Restart the board");
    shell_print_command_help("exit", "Exit this shell process");
    shell_write_line("");
    shell_write_line("Compatibility aliases:");
    shell_print_command_help("ls, cat, pwd, mkdir", "Accepted while older scripts are migrated");
    shell_print_command_help("rm, rmdir, clear", "Accepted while older scripts are migrated");
    shell_write_line("");
    shell_write_line("Program launch order:");
    shell_write_line("  1. Built-in command");
    shell_write_line("  2. Search PATH directories for <command> or <command>.exe");
    shell_write_line("  3. Spawn the matching executable when found");
    shell_write_line("");
    shell_write_line("Launch options:");
    shell_write_line("  command [--name task] [-- args...]");
    shell_write_line("  run [--name task] <path> [-- args...]");
    shell_write_line("  start [--name task] <command|path> [-- args...]");
}

static void shell_command_path(ShellState* state, const char* full_args) {
    char buffer[SHELL_LINE_MAX];
    char* tokens[SHELL_TOKEN_MAX];
    int count;
    int index;

    if (!full_args || full_args[0] == '\0') {
        shell_print_search_paths(state);
        return;
    }

    shell_copy_line(buffer, full_args, sizeof(buffer));
    count = shell_tokenize(buffer, tokens, SHELL_TOKEN_MAX);
    if (count <= 0) {
        shell_print_search_paths(state);
        return;
    }

    if (shell_same_text_case_insensitive(tokens[0], "show")) {
        shell_print_search_paths(state);
        return;
    }
    if (shell_same_text_case_insensitive(tokens[0], "clear")) {
        state->search_path_count = 0;
        shell_print_search_paths(state);
        return;
    }
    if (shell_same_text_case_insensitive(tokens[0], "reset")) {
        shell_reset_search_paths(state);
        shell_print_search_paths(state);
        return;
    }
    if (shell_same_text_case_insensitive(tokens[0], "add") || shell_same_text_case_insensitive(tokens[0], "set")) {
        if (count < 2) {
            shell_print_usage("path add <dir> [dir...] | path set <dir> [dir...] | path reset | path clear");
            return;
        }
        if (shell_same_text_case_insensitive(tokens[0], "set")) {
            state->search_path_count = 0;
        }
        for (index = 1; index < count; index++) {
            if (shell_add_search_path(state, tokens[index]) != 0) {
                shell_print_error("path", "too many search paths");
                return;
            }
        }
        shell_print_search_paths(state);
        return;
    }

    shell_print_usage("path add <dir> [dir...] | path set <dir> [dir...] | path reset | path clear");
}

static void shell_command_pwd(ShellState* state) {
    shell_write_line(state->cwd);
}

static void shell_command_cd(ShellState* state, const char* arg) {
    UserDirectoryEntry entry;
    char resolved[SHELL_PATH_MAX];
    long status;

    if (!arg || arg[0] == '\0') {
        shell_write_line(state->cwd);
        return;
    }

    shell_resolve_path(state->cwd, arg, resolved);
    status = readDirectoryEntry(resolved, 0, &entry);
    if (status < 0) {
        shell_print_error("cd", "directory not found");
        return;
    }
    shell_strncpy(state->cwd, resolved, sizeof(state->cwd));
    state->cwd[sizeof(state->cwd) - 1] = '\0';
}

static void shell_command_ls(ShellState* state, const char* arg) {
    UserDirectoryEntry entry;
    char size_text[32];
    char path[SHELL_PATH_MAX];
    unsigned long index = 0;
    unsigned long count = 0;
    unsigned long file_count = 0;
    unsigned long directory_count = 0;
    unsigned long total_bytes = 0;

    shell_resolve_path(state->cwd, (arg && arg[0] != '\0') ? arg : state->cwd, path);
    shell_write(" Directory of ");
    shell_write_line(path);
    shell_write("\n");

    while (1) {
        long status = readDirectoryEntry(path, index, &entry);
        if (status < 0) {
            shell_print_error("dir", "directory not found");
            return;
        }
        if (status == 0) {
            if (count == 0) {
                shell_write_line("  <empty>");
            }
            shell_write("\n  ");
            shell_write_ulong(file_count, 4);
            shell_write(" File(s)  ");
            shell_write_ulong(total_bytes, 1);
            shell_write_line(" bytes");
            shell_write("  ");
            shell_write_ulong(directory_count, 4);
            shell_write_line(" Dir(s)");
            return;
        }

        if ((entry.attr & 0x10) != 0) {
            directory_count++;
            shell_write("  <DIR>          ");
        }
        else {
            char* cursor = size_text;

            file_count++;
            total_bytes += entry.size;
            cursor = appendUnsignedLong(cursor, entry.size);
            *cursor = '\0';
            shell_write("  ");
            shell_write_right(size_text, 13);
            shell_write(" ");
        }
        shell_write_line(entry.name);
        index++;
        count++;
    }
}

static void shell_command_cat(ShellState* state, const char* arg) {
    char path[SHELL_PATH_MAX];
    char chunk[129];
    unsigned long offset = 0;

    if (!arg || arg[0] == '\0') {
        shell_print_usage("type <path>");
        return;
    }

    shell_resolve_path(state->cwd, arg, path);
    while (1) {
        long read = readFile(path, offset, chunk, sizeof(chunk) - 1);
        if (read < 0) {
            shell_print_error("type", "file not found");
            return;
        }
        if (read == 0) {
            shell_write("\n");
            return;
        }

        chunk[read] = '\0';
        shell_write(chunk);
        offset += (unsigned long)read;
    }
}

static void shell_command_mkdir(ShellState* state, const char* arg) {
    char path[SHELL_PATH_MAX];

    if (!arg || arg[0] == '\0') {
        shell_print_usage("md <path>");
        return;
    }

    shell_resolve_path(state->cwd, arg, path);
    if (makeDirectory(path) != 0) {
        shell_print_error("md", "failed");
        return;
    }
}

/*
 * shell_command_remove
 *
 * DEL, RD, and RM all land on the same kernel VFS remove operation. Keeping a
 * single implementation here avoids diverging semantics while the command
 * surface is being moved away from the older Unix-like shell vocabulary.
 */
static void shell_command_remove(ShellState* state, const char* arg, const char* verb) {
    char path[SHELL_PATH_MAX];

    if (!arg || arg[0] == '\0') {
        shell_write("syntax: ");
        shell_write(verb);
        shell_write_line(" <path>");
        return;
    }

    shell_resolve_path(state->cwd, arg, path);
    if (removePath(path) != 0) {
        shell_print_error(verb, "failed");
        return;
    }
}

static void shell_default_name(const char* path, char* name, int size) {
    const char* base = path;
    int index = 0;

    while (*path != '\0') {
        if (shell_is_path_separator(*path)) {
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
        shell_strcpy(name, "app");
    }
}

static void shell_command_run(ShellState* state, const char* full_args) {
    char buffer[SHELL_LINE_MAX];
    char args[SHELL_LAUNCH_ARGS_MAX];
    char* tokens[SHELL_TOKEN_MAX];
    const char* path_arg;
    const char* name_arg;
    int count;
    int next_index;
    int used_separator;

    if (!full_args || full_args[0] == '\0') {
        shell_print_usage("run [--name task] <path> [-- args]");
        return;
    }

    shell_copy_line(buffer, full_args, sizeof(buffer));
    count = shell_tokenize(buffer, tokens, SHELL_TOKEN_MAX);
    if (count <= 0 || shell_parse_launch_options(tokens, count, &next_index, &name_arg, &used_separator) != 0 || next_index >= count) {
        shell_print_usage("run [--name task] <path> [-- args]");
        return;
    }

    path_arg = tokens[next_index++];
    if (!name_arg && !used_separator && next_index + 1 == count) {
        name_arg = tokens[next_index];
        args[0] = '\0';
    }
    else {
        shell_join_tokens(args, sizeof(args), tokens, next_index, count);
    }

    next_index = shell_try_spawn_target(state, path_arg, name_arg, args);
    if (next_index == 1) {
        shell_print_error("run", "program not found");
    }
    else if (next_index != 0) {
        shell_print_error("run", "failed to start program");
    }
}

/*
 * shell_command_start
 *
 * The shell needs one explicit background-launch verb so event monitors can
 * keep streaming output while the interactive prompt remains available.
 */
static void shell_command_start(ShellState* state, const char* full_args) {
    char buffer[SHELL_LINE_MAX];
    char args[SHELL_LAUNCH_ARGS_MAX];
    char* tokens[SHELL_TOKEN_MAX];
    const char* target_arg;
    const char* name_arg;
    int count;
    int next_index;
    int used_separator;
    int result;

    if (!full_args || full_args[0] == '\0') {
        shell_print_usage("start [--name task] <command|path> [-- args]");
        return;
    }

    shell_copy_line(buffer, full_args, sizeof(buffer));
    count = shell_tokenize(buffer, tokens, SHELL_TOKEN_MAX);
    if (count <= 0 || shell_parse_launch_options(tokens, count, &next_index, &name_arg, &used_separator) != 0 || next_index >= count) {
        shell_print_usage("start [--name task] <command|path> [-- args]");
        return;
    }

    target_arg = tokens[next_index++];
    shell_join_tokens(args, sizeof(args), tokens, next_index, count);

    result = shell_try_spawn_target_nowait(state, target_arg, name_arg, args);
    if (result == 1) {
        shell_print_error("start", "program not found");
    }
    else if (result != 0) {
        shell_print_error("start", "failed to start program");
    }
}

/*
 * shell_command_ps
 *
 * Show only task fields that the kernel can describe truthfully today. The old
 * COUNT column was a global scheduler tick snapshot, not per-task CPU usage,
 * so the table now spends that space on the wait reason users actually need.
 *
 * @return Nothing.
 */
static void shell_command_ps(void) {
    long pid;
    int count = 0;

    shell_print_heading("Tasks");
    shell_write("  ");
    shell_write_left("PID", 4);
    shell_write_spaces(2);
    shell_write_left("STATE", 8);
    shell_write_spaces(2);
    shell_write_left("WAIT", 8);
    shell_write_spaces(2);
    shell_write_left("PRIO", 4);
    shell_write_spaces(2);
    shell_write_line("NAME");
    shell_write("  ");
    shell_write_left("----", 4);
    shell_write_spaces(2);
    shell_write_left("--------", 8);
    shell_write_spaces(2);
    shell_write_left("--------", 8);
    shell_write_spaces(2);
    shell_write_left("----", 4);
    shell_write_spaces(2);
    shell_write_line("------------------------------");

    for (pid = 0; pid < SHELL_TASK_SCAN_MAX; pid++) {
        UserTaskInfo info;
        long status = getTaskInfo(pid, &info);
        const char* state;
        const char* wait_reason;

        // Task queries follow the same convention: 0 means success, negative means the PID is absent or invalid.
        if (status < 0) {
            continue;
        }
        state = shell_task_state_name(info.main_thread_state);
        wait_reason = "-";
        // A wait reason is only actionable while the task is actually parked.
        // Keeping runnable tasks on "-" avoids stale-looking noise in `ps`.
        if (info.main_thread_state == USER_TASK_STATE_WAITING) {
            wait_reason = shell_task_wait_reason_name(info.wait_reason);
        }

        shell_write("  ");
        shell_write_long(info.id, 4);
        shell_write_spaces(2);
        shell_write_left(state, 8);
        shell_write_spaces(2);
        shell_write_left(wait_reason, 8);
        shell_write_spaces(2);
        shell_write_long(info.current_priority, 4);
        shell_write_spaces(2);
        shell_write_line(info.name);
        count++;
    }

    if (count == 0) {
        shell_write_line("  (no tasks)");
    }
}

static void shell_command_mem(void) {
    UserMemInfo info;

    if (getMemoryInfo(&info) != 0) {
        shell_print_error("mem", "unavailable");
        return;
    }

    shell_print_heading("Memory");
    shell_print_number_value("total", info.total_bytes / 1024, "KB");
    shell_print_number_value("free", info.free_bytes / 1024, "KB");
    shell_print_number_value("pages", info.free_pages, 0);
    shell_print_number_value("page size", info.page_size, "bytes");
}

static void shell_command_guidemo(void) {
    static const unsigned int demo_points[][2] = {
        { 120U, 96U },
        { 860U, 96U },
        { 860U, 320U },
        { 120U, 320U },
        { 490U, 208U },
    };
    unsigned int index;

    if (shell_gui_write_text("\n[guidemo] virt framebuffer keyboard path is live\n") != 0) {
        shell_print_error("guidemo", "framebuffer gui unavailable");
        return;
    }

    (void)shell_gui_write_text("[guidemo] shell typing is mirrored into the GUI\n");
    (void)shell_gui_write_text("[guidemo] pointer path below is software-injected today\n");

    shell_write_line("guidemo: mirrored text into the framebuffer GUI");
    shell_write_line("guidemo: animating a pointer path in the GUI");
    shell_write_line("guidemo: keep typing in this shell to exercise keyboard mirroring");
    shell_write_line("guidemo: real QEMU mouse/touch input still needs a guest HID driver");

    for (index = 0; index < (sizeof(demo_points) / sizeof(demo_points[0])); index++) {
        if (shell_gui_push_pointer(demo_points[index][0], demo_points[index][1]) != 0) {
            shell_print_error("guidemo", "pointer injection failed");
            return;
        }
        sleepMs(180);
    }

    (void)shell_gui_push_pointer(ROS_KERNEL_GUI_POINTER_HIDDEN, ROS_KERNEL_GUI_POINTER_HIDDEN);
    sleepMs(120);
    (void)shell_gui_push_pointer(demo_points[(sizeof(demo_points) / sizeof(demo_points[0])) - 1U][0], demo_points[(sizeof(demo_points) / sizeof(demo_points[0])) - 1U][1]);
}

static void shell_command_kdebug(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print_usage("kdebug <kernel command>");
        return;
    }

    if (runDebugShell(args) != 0) {
        shell_print_error("kdebug", "command failed");
    }
}

static void shell_command_ipc_test(void) {
    long receiver_pid;
    long sender_pid;

    shell_print_heading("IPC Test");
    shell_write_line("starting receiver");

    receiver_pid = shell_spawn_program_nowait("C:\\bin\\ipc_demo.exe", "ipc-recv", "recv");
    if (receiver_pid < 0) {
        shell_print_error("ipc-test", "failed to start receiver");
        return;
    }

    sleepMs(500);

    shell_write_line("starting sender");
    sender_pid = shell_spawn_program_nowait("C:\\bin\\ipc_demo.exe", "ipc-send", "send ipc-recv");
    if (sender_pid < 0) {
        shell_print_error("ipc-test", "failed to start sender");
        return;
    }

    shell_wait_for_program_exit(receiver_pid);
    shell_wait_for_program_exit(sender_pid);
    shell_write_line("ipc-test: complete");
}

static void shell_command_modules(const char* arg) {
    (void)arg;

    /*
     * The old shell queried the legacy kernel module runtime here. That
     * runtime was intentionally removed when schedproc and the loader-based
     * userspace model took over, so keep the command but report the new
     * contract explicitly instead of routing into a dead subsystem.
     */
    shell_write_line("modules: legacy kernel module runtime is disabled");
}

static void shell_command_kill(const char* arg) {
    int ok;
    long pid;

    if (!arg || arg[0] == '\0') {
        shell_print_usage("kill <pid>");
        return;
    }

    pid = shell_parse_long(arg, &ok);
    if (!ok) {
        shell_print_error("kill", "invalid pid");
        return;
    }
    if (killTask(pid) != 0) {
        shell_print_error("kill", "failed");
        return;
    }
    shell_write("kill: terminated ");
    shell_write_long(pid, 1);
    shell_write("\n");
}

static int shell_execute_builtin(ShellState* state, const char* command, const char* arg0, const char* full_args) {
    if (shell_same_text_case_insensitive(command, "help")) {
        shell_print_help();
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "exit")) {
        shell_write_line("shell: exiting");
        exitProcess(0);
    }
    if (shell_same_text_case_insensitive(command, "path")) {
        shell_command_path(state, full_args);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "pwd")) {
        shell_command_pwd(state);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "cd")) {
        shell_command_cd(state, arg0);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "dir") || shell_same_text_case_insensitive(command, "ls")) {
        shell_command_ls(state, arg0);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "type") || shell_same_text_case_insensitive(command, "cat")) {
        shell_command_cat(state, arg0);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "md") || shell_same_text_case_insensitive(command, "mkdir")) {
        shell_command_mkdir(state, arg0);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "del") || shell_same_text_case_insensitive(command, "erase") || shell_same_text_case_insensitive(command, "rm")) {
        shell_command_remove(state, arg0, command);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "rd") || shell_same_text_case_insensitive(command, "rmdir")) {
        shell_command_remove(state, arg0, command);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "run")) {
        shell_command_run(state, full_args);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "start")) {
        shell_command_start(state, full_args);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "modules")) {
        shell_command_modules(arg0);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "ps")) {
        shell_command_ps();
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "mem")) {
        shell_command_mem();
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "guidemo")) {
        shell_command_guidemo();
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "kdebug")) {
        shell_command_kdebug(full_args);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "ipc-test")) {
        shell_command_ipc_test();
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "kill")) {
        shell_command_kill(arg0);
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "reboot")) {
        shell_write_line("reboot: restarting system");
        if (rebootSystem() < 0) {
            shell_write_line("reboot: kernel rejected reboot request");
        }
        return 0;
    }
    if (shell_same_text_case_insensitive(command, "cls") || shell_same_text_case_insensitive(command, "clear")) {
        shell_write("\033[2J\033[H");
        return 0;
    }

    return -1;
}

static int shell_try_external_command(ShellState* state, const char* command, const char* full_args) {
    char buffer[SHELL_LINE_MAX];
    char args[SHELL_LAUNCH_ARGS_MAX];
    char* tokens[SHELL_TOKEN_MAX];
    const char* name_arg;
    int count = 0;
    int next_index;
    int used_separator;

    if (full_args && full_args[0] != '\0') {
        shell_copy_line(buffer, full_args, sizeof(buffer));
        count = shell_tokenize(buffer, tokens, SHELL_TOKEN_MAX);
        if (shell_parse_launch_options(tokens, count, &next_index, &name_arg, &used_separator) != 0) {
            shell_print_error("shell", "invalid launch options");
            return 0;
        }
        shell_join_tokens(args, sizeof(args), tokens, next_index, count);
    }
    else {
        name_arg = 0;
        args[0] = '\0';
    }

    return shell_try_spawn_target(state, command, name_arg, args);
}

static void shell_execute(ShellState* state, const char* line) {
    char command_line[SHELL_LINE_MAX];
    char original_line[SHELL_LINE_MAX];
    char* cursor;
    char* original_cursor;
    char* command;
    char* arg0;
    char* full_args;
    int external_status;

    shell_copy_line(command_line, line, sizeof(command_line));
    shell_copy_line(original_line, line, sizeof(original_line));
    cursor = command_line;
    original_cursor = original_line;
    command = shell_next_token(&cursor);
    shell_next_token(&original_cursor);
    full_args = shell_skip_spaces(original_cursor);
    arg0 = shell_next_token(&cursor);

    if (!command) {
        return;
    }
    if (shell_execute_builtin(state, command, arg0, full_args) == 0) {
        return;
    }

    external_status = shell_try_external_command(state, command, full_args);
    if (external_status == 0) {
        return;
    }

    if (external_status == 1) {
        shell_write("shell: '");
        shell_write(command);
        shell_write_line("' is not recognized as a command or executable");
        return;
    }

    {
        char buffer[128];

        snprintf(buffer, sizeof(buffer), "shell: failed to start '%s' status=%d", command, external_status);
        shell_write_line(buffer);
    }
}

/*
 * AppMain
 *
 * Userspace programs in this kernel are packed with `AppMain` as the image
 * entry symbol. The restored legacy shell used a hosted-style `main()`, which
 * linked into an almost-empty image because the packer set `-e AppMain` and
 * then garbage-collected every unreferenced section.
 *
 * Restoring the expected entry point makes the loader see a real executable
 * again and keeps the shell aligned with every other user application in the
 * tree.
 */
int AppMain(void) {
    ShellState state;
    char command_line[SHELL_LINE_MAX];
    char line[SHELL_LINE_MAX];

    shell_bzero(&state, sizeof(state));
    shell_strcpy(state.cwd, "C:\\");
    shell_reset_search_paths(&state);

    shell_write("\nROS Command Shell\nType HELP for a list of commands.\n\n");

    debugInfo("shell.exe: starting");

    for (;;) {
        shell_read_line(&state, line, sizeof(line));
        if (line[0] == '\0') {
            continue;
        }

        shell_copy_line(command_line, line, sizeof(command_line));
        shell_add_history(&state, command_line);
        shell_execute(&state, command_line);
    }
}
