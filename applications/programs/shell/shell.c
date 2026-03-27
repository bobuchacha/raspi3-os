#include "app/kernel.h"
#include "app/kernel_gui.h"
#include "stdio.h"
#include "logger.h"

#define SHELL_LINE_MAX 128
#define SHELL_HISTORY_MAX 16
#define SHELL_PATH_MAX 256
#define SHELL_TASK_SCAN_MAX 64
#define SHELL_SEARCH_PATH_MAX 8
#define SHELL_TASK_NAME_MAX 32
#define SHELL_LAUNCH_ARGS_MAX 128
#define SHELL_TOKEN_MAX 16

static const char* const g_shell_default_search_paths[] = {
    "/bin",
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

static void shell_forward_gui_key(char ch) {
    long result = 0;

    if (user_kernel_module_invoke("usbkey", "inject_key", (unsigned long)(unsigned char)ch, 0, &result) != 0) {
        user_kernel_module_invoke("fbgui", "push_key", (unsigned long)(unsigned char)ch, 0, &result);
    }
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
                user_kernel_write(buffer);
                index = 0;
            }
            buffer[index++] = '\r';
        }

        if (index >= (int)sizeof(buffer) - 1) {
            buffer[index] = '\0';
            user_kernel_write(buffer);
            index = 0;
        }

        buffer[index++] = *text++;
    }

    if (index > 0) {
        buffer[index] = '\0';
        user_kernel_write(buffer);
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

    fprint(buffer, (char*)"%lu", value);
    shell_write_right(buffer, width);
}

static void shell_write_long(long value, int width) {
    char buffer[32];

    fprint(buffer, (char*)"%ld", value);
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

    if (unit && unit[0] != '\0') {
        fprint(buffer, (char*)"%lu %s", value, (char*)unit);
    }
    else {
        fprint(buffer, (char*)"%lu", value);
    }
    shell_print_key_value(key, buffer);
}

static void shell_print_error(const char* command, const char* message) {
    shell_write(command);
    shell_write(": ");
    shell_write_line(message);
}

static void shell_print_usage(const char* usage) {
    shell_write("usage: ");
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
    fprint(prompt, (char*)"ros:%s$ ", (char*)state->cwd);
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
        fprint(buffer, (char*)"\033[%dD", length - cursor);
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
    return (int)user_kernel_console_read();
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

    if (length <= 1) {
        shell_strcpy(path, "/");
        return;
    }
    while (length > 1 && path[length - 1] == '/') {
        length--;
    }
    while (length > 1 && path[length - 1] != '/') {
        length--;
    }
    if (length > 1) {
        path[length - 1] = '\0';
    }
    else {
        path[1] = '\0';
    }
}

static void shell_append_segment(char* path, const char* segment) {
    if (shell_strlen(path) > 1) {
        shell_strcat(path, "/");
    }
    shell_strcat(path, segment);
}

static int shell_path_has_extension(const char* path) {
    const char* base = path;

    while (*path != '\0') {
        if (*path == '/') {
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
        if (*path == '/') {
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

    app_log_trace("shell", "spawning program '%s' with name_arg='%s' and args_arg='%s'", path, name_arg ? name_arg : "<null>", args_arg ? args_arg : "<null>");

    if (name_arg && name_arg[0] != '\0') {
        shell_strncpy(name, name_arg, sizeof(name));
        name[sizeof(name) - 1] = '\0';
    }
    else {
        shell_default_name(path, name, sizeof(name));
    }

    app_log_trace("shell", "resolved spawn name '%s'", name);
    pid = user_kernel_spawn_with_args(path, name, args_arg);
    app_log_trace("shell", "spawned program '%s' with pid=%ld", path, pid);
    if (pid < 0) {
        return -1;
    }

    shell_print_heading("Spawned task");
    fprint(buffer, (char*)"%ld", pid);
    shell_print_key_value("pid", buffer);
    shell_print_key_value("name", name);
    shell_print_key_value("path", path);
    if (args_arg && args_arg[0] != '\0') {
        shell_print_key_value("args", args_arg);
    }
    return 0;
}

static int shell_file_exists(const char* path) {
    char probe;
    long status;

    if (!path || path[0] == '\0') {
        return 0;
    }

    // Probe the candidate path before spawning so invalid commands do not create transient tasks.
    status = user_kernel_read_file(path, 0, &probe, 1);
    return status >= 0;
}

static int shell_try_spawn_candidate(const char* path, const char* name_arg, const char* args_arg) {
    app_log_trace("shell", "trying candidate path '%s'", path);
    if (!shell_file_exists(path)) {
        return 0;
    }

    // Spawn only after the candidate path is confirmed to be a readable file.
    return shell_spawn_program(path, name_arg, args_arg) == 0 ? 1 : -1;
}

static int shell_search_path_index(const ShellState* state, const char* path) {
    int index;

    for (index = 0; index < state->search_path_count; index++) {
        if (shell_strcmp(state->search_paths[index], path) == 0) {
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
        if (shell_strcmp(tokens[index], "--name") == 0) {
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

    app_log_trace("shell", "searching for external command '%s'", input);

    if (!input || input[0] == '\0') {
        return -1;
    }

    if (shell_path_contains_separator(input) || input[0] == '/') {
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
                return -2;
            }
        }
        result = shell_try_spawn_candidate(candidate, name_arg, args_arg);
        if (result > 0) {
            return 0;
        }
        return result < 0 ? -2 : -1;
    }

    for (index = 0; index < state->search_path_count; index++) {
        shell_strncpy(candidate, state->search_paths[index], sizeof(candidate));
        candidate[sizeof(candidate) - 1] = '\0';
        if (shell_strlen(candidate) > 1) {
            shell_strcat(candidate, "/");
        }
        shell_strcat(candidate, input);
        if (!shell_path_has_extension(candidate) && shell_strlen(candidate) + 4 < sizeof(with_ext)) {
            shell_strncpy(with_ext, candidate, sizeof(with_ext));
            with_ext[sizeof(with_ext) - 1] = '\0';
            shell_strcat(with_ext, ".exe");
            result = shell_try_spawn_candidate(with_ext, name_arg, args_arg);
            if (result > 0) {
                return 0;
            }
            if (result < 0) {
                return -2;
            }
        }
        result = shell_try_spawn_candidate(candidate, name_arg, args_arg);
        if (result > 0) {
            return 0;
        }
        if (result < 0) {
            return -2;
        }
    }

    return -1;
}

static void shell_resolve_path(const char* cwd, const char* input, char* output) {
    char token[128];
    int token_length = 0;

    if (!input || input[0] == '\0') {
        shell_strncpy(output, cwd, SHELL_PATH_MAX);
        output[SHELL_PATH_MAX - 1] = '\0';
        return;
    }

    if (input[0] == '/') {
        shell_strcpy(output, "/");
        input++;
    }
    else {
        shell_strncpy(output, cwd, SHELL_PATH_MAX);
        output[SHELL_PATH_MAX - 1] = '\0';
    }

    for (;;) {
        if (*input == '/' || *input == '\0') {
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
            input++;
            continue;
        }
        if (token_length < (int)sizeof(token) - 1) {
            token[token_length++] = *input;
        }
        input++;
    }

    if (output[0] == '\0') {
        shell_strcpy(output, "/");
    }
}

static void shell_print_help(void) {
    shell_print_heading("Commands");
    shell_print_command_help("help", "Show this help");
    shell_print_command_help("path [subcommand]", "Show or change executable search paths");
    shell_print_command_help("pwd", "Print current directory");
    shell_print_command_help("cd [path]", "Change current directory");
    shell_print_command_help("ls [path]", "List a directory");
    shell_print_command_help("cat <path>", "Print a file");
    shell_print_command_help("mkdir <path>", "Create a directory");
    shell_print_command_help("run [--name n] <path> [-- args]", "Spawn a user program");
    shell_print_command_help("modules [name]", "List modules or inspect one export table");
    shell_print_command_help("ps", "List tasks");
    shell_print_command_help("mem", "Show memory usage");
    shell_print_command_help("kdebug <command>", "Run one kernel debug-shell command");
    shell_print_command_help("kill <pid>", "Terminate a task");
    shell_print_command_help("reboot", "Restart the board");
    shell_print_command_help("clear", "Clear the screen");
    shell_write_line("");
    shell_write_line("Execution order:");
    shell_write_line("  1. Built-in command");
    shell_write_line("  2. Search configured paths for <command> or <command>.exe");
    shell_write_line("  3. Spawn the matching executable when found");
    shell_write_line("");
    shell_write_line("Launch options:");
    shell_write_line("  command [--name task] [-- args...]");
    shell_write_line("  run [--name task] <path> [-- args...]");
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

    if (shell_strcmp(tokens[0], "show") == 0) {
        shell_print_search_paths(state);
        return;
    }
    if (shell_strcmp(tokens[0], "clear") == 0) {
        state->search_path_count = 0;
        shell_print_search_paths(state);
        return;
    }
    if (shell_strcmp(tokens[0], "reset") == 0) {
        shell_reset_search_paths(state);
        shell_print_search_paths(state);
        return;
    }
    if (shell_strcmp(tokens[0], "add") == 0 || shell_strcmp(tokens[0], "set") == 0) {
        if (count < 2) {
            shell_print_usage("path add <dir> [dir...] | path set <dir> [dir...] | path reset | path clear");
            return;
        }
        if (shell_strcmp(tokens[0], "set") == 0) {
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
    shell_print_key_value("cwd", state->cwd);
}

static void shell_command_cd(ShellState* state, const char* arg) {
    UserDirectoryEntry entry;
    char resolved[SHELL_PATH_MAX];
    long status;

    shell_resolve_path(state->cwd, (arg && arg[0] != '\0') ? arg : "/", resolved);
    status = user_kernel_dir_entry(resolved, 0, &entry);
    if (status < 0) {
        shell_print_error("cd", "directory not found");
        return;
    }
    shell_strncpy(state->cwd, resolved, sizeof(state->cwd));
    state->cwd[sizeof(state->cwd) - 1] = '\0';
    shell_print_key_value("cwd", state->cwd);
}

static void shell_command_ls(ShellState* state, const char* arg) {
    UserDirectoryEntry entry;
    char path[SHELL_PATH_MAX];
    unsigned long index = 0;
    unsigned long count = 0;

    shell_resolve_path(state->cwd, (arg && arg[0] != '\0') ? arg : state->cwd, path);
    shell_print_heading("Directory listing");
    shell_print_key_value("path", path);
    shell_write("\n");
    shell_write_left("  TYPE", 10);
    shell_write_left("SIZE", 12);
    shell_write_line("NAME");
    shell_write_left("  --------", 10);
    shell_write_left("----------", 12);
    shell_write_line("------------------------------");

    while (1) {
        long status = user_kernel_dir_entry(path, index, &entry);
        if (status < 0) {
            shell_print_error("ls", "directory not found");
            return;
        }
        if (status == 0) {
            if (count == 0) {
                shell_write_line("  (empty)");
            }
            return;
        }

        shell_write("  ");
        shell_write_left((entry.attr & 0x10) ? "dir" : "file", 8);
        shell_write_ulong(entry.size, 12);
        shell_write("  ");
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
        shell_print_usage("cat <path>");
        return;
    }

    shell_resolve_path(state->cwd, arg, path);
    while (1) {
        long read = user_kernel_read_file(path, offset, chunk, sizeof(chunk) - 1);
        if (read < 0) {
            shell_print_error("cat", "file not found");
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
        shell_print_usage("mkdir <path>");
        return;
    }

    shell_resolve_path(state->cwd, arg, path);
    if (user_kernel_mkdir(path) != 0) {
        shell_print_error("mkdir", "failed");
        return;
    }
    shell_write("mkdir: created ");
    shell_write_line(path);
}

static void shell_default_name(const char* path, char* name, int size) {
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
    if (next_index == -1) {
        shell_print_error("run", "program not found");
    }
    else if (next_index != 0) {
        shell_print_error("run", "failed to start program");
    }
}

static void shell_command_ps(void) {
    long pid;
    int count = 0;

    shell_print_heading("Tasks");
    shell_write_left("  PID", 8);
    shell_write_left("STATE", 10);
    shell_write_left("COUNT", 12);
    shell_write_left("PRI", 6);
    shell_write_line("NAME");
    shell_write_left("  ------", 8);
    shell_write_left("--------", 10);
    shell_write_left("----------", 12);
    shell_write_left("----", 6);
    shell_write_line("------------------------------");

    for (pid = 0; pid < SHELL_TASK_SCAN_MAX; pid++) {
        UserTaskInfo info;
        long status = user_kernel_task_info(pid, &info);
        const char* state = "?";

        if (status <= 0) {
            continue;
        }
        if (info.state == 1) {
            state = "READY";
        }
        else if (info.state == 2) {
            state = "RUN";
        }
        else if (info.state == 3) {
            state = "SLEEP";
        }
        else if (info.state == 4) {
            state = "BLOCK";
        }
        else if (info.state == 0) {
            state = "ZOMB";
        }

        shell_write("  ");
        shell_write_long(info.id, 4);
        shell_write_spaces(2);
        shell_write_left(state, 8);
        shell_write_ulong(info.counter, 10);
        shell_write_spaces(2);
        shell_write_long(info.priority, 4);
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

    if (user_kernel_mem_info(&info) != 0) {
        shell_print_error("mem", "unavailable");
        return;
    }

    shell_print_heading("Memory");
    shell_print_number_value("total", info.total_bytes / 1024, "KB");
    shell_print_number_value("free", info.free_bytes / 1024, "KB");
    shell_print_number_value("pages", info.free_pages, 0);
    shell_print_number_value("page size", info.page_size, "bytes");
}

static void shell_command_kdebug(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print_usage("kdebug <kernel command>");
        return;
    }

    if (user_kernel_debug_shell(args) != 0) {
        shell_print_error("kdebug", "command failed");
    }
}

static void shell_command_modules(const char* arg) {
    char command[96];

    shell_strcpy(command, "modules");
    if (arg && arg[0] != '\0') {
        if (shell_strlen(arg) + shell_strlen(command) + 2 >= sizeof(command)) {
            shell_print_error("modules", "name too long");
            return;
        }
        shell_strcat(command, " ");
        shell_strcat(command, arg);
    }

    if (user_kernel_debug_shell(command) != 0) {
        shell_print_error("modules", "command failed");
    }
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
    if (user_kernel_kill(pid) != 0) {
        shell_print_error("kill", "failed");
        return;
    }
    shell_write("kill: terminated ");
    shell_write_long(pid, 1);
    shell_write("\n");
}

static int shell_execute_builtin(ShellState* state, const char* command, const char* arg0, const char* full_args) {
    if (shell_strcmp(command, "help") == 0) {
        shell_print_help();
        return 0;
    }
    if (shell_strcmp(command, "path") == 0) {
        shell_command_path(state, full_args);
        return 0;
    }
    if (shell_strcmp(command, "pwd") == 0) {
        shell_command_pwd(state);
        return 0;
    }
    if (shell_strcmp(command, "cd") == 0) {
        shell_command_cd(state, arg0);
        return 0;
    }
    if (shell_strcmp(command, "ls") == 0) {
        shell_command_ls(state, arg0);
        return 0;
    }
    if (shell_strcmp(command, "cat") == 0) {
        shell_command_cat(state, arg0);
        return 0;
    }
    if (shell_strcmp(command, "mkdir") == 0) {
        shell_command_mkdir(state, arg0);
        return 0;
    }
    if (shell_strcmp(command, "run") == 0) {
        shell_command_run(state, full_args);
        return 0;
    }
    if (shell_strcmp(command, "modules") == 0) {
        shell_command_modules(arg0);
        return 0;
    }
    if (shell_strcmp(command, "ps") == 0) {
        shell_command_ps();
        return 0;
    }
    if (shell_strcmp(command, "mem") == 0) {
        shell_command_mem();
        return 0;
    }
    if (shell_strcmp(command, "kdebug") == 0) {
        shell_command_kdebug(full_args);
        return 0;
    }
    if (shell_strcmp(command, "kill") == 0) {
        shell_command_kill(arg0);
        return 0;
    }
    if (shell_strcmp(command, "reboot") == 0) {
        shell_write_line("reboot: restarting system");
        user_kernel_reboot();
        return 0;
    }
    if (shell_strcmp(command, "clear") == 0) {
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

    app_log_trace("shell", "attempting to spawn external command '%s'", command);

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

    if (external_status == -1) {
        shell_write("shell: invalid command '");
        shell_write(command);
        shell_write_line("'");
        return;
    }

    shell_write("shell: failed to start '");
    shell_write(command);
    shell_write_line("'");
}

long main(void) {
    ShellState state;
    char command_line[SHELL_LINE_MAX];
    char line[SHELL_LINE_MAX];

    shell_bzero(&state, sizeof(state));
    shell_strcpy(state.cwd, "/");
    shell_reset_search_paths(&state);

    shell_write("\nROS userspace shell\nType 'help' for commands.\n\n");
    app_log_debug("shell", "initialized");

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
