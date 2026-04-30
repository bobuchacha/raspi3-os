#include "app/app.h"

#define DLLSMOKE_MODULE_NAME "samplemath.dll"
#define DLLSMOKE_SHARED_NAME_PARENT "Jenny"

// typedef long (*dllsmoke_add3_fn)(long lhs, long rhs, long extra);
typedef unsigned long (*dllsmoke_invocation_count_fn)(void);
typedef const char* (*dllsmoke_profile_fn)(void);
typedef long (*dllsmoke_set_name_fn)(const char* value);
typedef const char* (*dllsmoke_get_name_fn)(void);

// DLL_IMPORT_FUNCTION(DLLSMOKE_MODULE_NAME, "SampleMathAdd3", dllsmoke_add3_fn, g_dllsmoke_add3);
DLL_IMPORT_FUNCTION(DLLSMOKE_MODULE_NAME, "SampleMathInvocationCount", dllsmoke_invocation_count_fn, g_dllsmoke_invocation_count);
DLL_IMPORT_FUNCTION(DLLSMOKE_MODULE_NAME, "SampleMathProfile", dllsmoke_profile_fn, g_dllsmoke_profile);
DLL_IMPORT_FUNCTION(DLLSMOKE_MODULE_NAME, "SampleMathSetName", dllsmoke_set_name_fn, g_dllsmoke_set_name);
DLL_IMPORT_FUNCTION(DLLSMOKE_MODULE_NAME, "SampleMathGetName", dllsmoke_get_name_fn, g_dllsmoke_get_name);

DECLARE(long, add3, (long first_number, long second_number, long third_number), FROM, "samplemath.dll", "SampleMathAdd3");

/*
 * dllsmoke_text_equals
 *
 * The shared-memory smoke test only needs one tiny freestanding comparator to
 * validate the name observed by different processes.
 *
 * @param lhs Left-hand string.
 * @param rhs Right-hand string.
 * @return Non-zero when both strings contain the same bytes.
 */
static int dllsmoke_text_equals(const char* lhs, const char* rhs) {
    unsigned long index = 0UL;

    if (lhs == rhs) {
        return 1;
    }
    if (lhs == 0 || rhs == 0) {
        return 0;
    }

    while (lhs[index] != '\0' && rhs[index] != '\0') {
        if (lhs[index] != rhs[index]) {
            return 0;
        }
        ++index;
    }

    return lhs[index] == rhs[index];
}

/*
 * dllsmoke_append_long
 *
 * The smoke output needs predictable integer formatting without depending on a
 * hosted libc implementation inside EL0.
 *
 * @param destination Output cursor inside a caller-owned buffer.
 * @param value Signed value to append.
 * @return Advanced output cursor.
 */
static char* dllsmoke_append_long(char* destination, long value) {
    if (value < 0) {
        *destination++ = '-';
        return appendUnsignedLong(destination, (unsigned long)(-value));
    }

    return appendUnsignedLong(destination, (unsigned long)value);
}

/*
 * dllsmoke_write_line
 *
 * Funnel user-visible output through one wrapper so the sample stays small and
 * every status line uses the same console path.
 *
 * @param text Null-terminated line to emit.
 * @return Kernel write status.
 */
static long dllsmoke_write_line(const char* text) {
    return writeLine(text);
}

/*
 * dllsmoke_write_value
 *
 * Report one string-valued smoke datum.
 *
 * @param label Prefix text written before the value.
 * @param value Value text to append.
 * @return Kernel write status.
 */
static long dllsmoke_write_value(const char* label, const char* value) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, label);
    cursor = appendText(cursor, value != 0 && value[0] != '\0' ? value : "<none>");
    *cursor = '\0';
    return dllsmoke_write_line(line);
}

/*
 * dllsmoke_write_result
 *
 * Print one arithmetic result and the corresponding DLL-side invocation count.
 *
 * @param ordinal Human-readable call number.
 * @param total Returned arithmetic result.
 * @param count DLL-side invocation count after the call.
 * @return Kernel write status.
 */
static long dllsmoke_write_result(unsigned long ordinal, long total, unsigned long count) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, "dllsmoke.exe: result[");
    cursor = appendUnsignedLong(cursor, ordinal);
    cursor = appendText(cursor, "] total=");
    cursor = dllsmoke_append_long(cursor, total);
    cursor = appendText(cursor, " count=");
    cursor = appendUnsignedLong(cursor, count);
    *cursor = '\0';
    return dllsmoke_write_line(line);
}

static int u64_to_hex(uint64_t value, char* buffer, size_t buffer_size, int uppercase) {
    const char* digits = uppercase
        ? "0123456789ABCDEF"
        : "0123456789abcdef";

    // Max: 16 hex digits + null terminator
    if (buffer_size < 17)
        return -1;

    buffer[16] = '\0';

    for (int i = 15; i >= 0; --i) {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }

    return 0;
}

/*
 * main
 *
 * The import slots below are resolved before `_start` transfers control here.
 * Successful output therefore proves the executable import table, DLL load,
 * export lookup, and DLL process-attach flow all completed correctly.
 *
 * @return Zero on success, non-zero on smoke failure.
 */
int main(void) {
    char task_name[64] = { 0 };
    char task_args[128] = { 0 };
    const char* profile;
    const char* shared_name;
    long first_total;
    long second_total;
    unsigned long first_count;
    unsigned long second_count;

    if (getTaskName(task_name, sizeof(task_name)) < 0) {
        task_name[0] = '\0';
    }
    if (getTaskArgs(task_args, sizeof(task_args)) < 0) {
        task_args[0] = '\0';
    }

    (void)dllsmoke_write_line("dllsmoke.exe: starting import smoke");
    (void)dllsmoke_write_value("dllsmoke.exe: task=", task_name);
    (void)dllsmoke_write_value("dllsmoke.exe: args=", task_args);

    if (add3 == 0 || g_dllsmoke_invocation_count == 0 || g_dllsmoke_profile == 0 || g_dllsmoke_set_name == 0 || g_dllsmoke_get_name == 0) {
        (void)dllsmoke_write_line("dllsmoke.exe: import slot was not resolved");
        return 1;
    }

    profile = g_dllsmoke_profile();
    if (profile == 0 || profile[0] == '\0') {
        (void)dllsmoke_write_line("dllsmoke.exe: profile export failed");
        return 2;
    }

    first_total = add3(10, 20, 3);
    first_count = g_dllsmoke_invocation_count();
    second_total = add3(7, 8, 9);
    second_count = g_dllsmoke_invocation_count();

    (void)dllsmoke_write_value("dllsmoke.exe: profile=", profile);
    (void)dllsmoke_write_result(1UL, first_total, first_count);
    (void)dllsmoke_write_result(2UL, second_total, second_count);

    shared_name = g_dllsmoke_get_name();
    (void)dllsmoke_write_value("dllsmoke.exe: shared name before write=", shared_name ? shared_name : "<null>");

    if (g_dllsmoke_set_name(DLLSMOKE_SHARED_NAME_PARENT) < 0) {
        (void)dllsmoke_write_line("dllsmoke.exe: failed to publish shared name");
        return 4;
    }

    shared_name = g_dllsmoke_get_name();
    (void)dllsmoke_write_value("dllsmoke.exe: shared name after write=", shared_name ? shared_name : "<null>");
    if (!dllsmoke_text_equals(shared_name, DLLSMOKE_SHARED_NAME_PARENT)) {
        (void)dllsmoke_write_line("dllsmoke.exe: parent shared name mismatch");
        return 5;
    }

    if (first_total != 34 || first_count != 1UL || second_total != 26 || second_count != 2UL) {
        (void)dllsmoke_write_line("dllsmoke.exe: unexpected DLL result");
        return 3;
    }

    // write address of add3
    char add3_address[17];
    if (u64_to_hex((uint64_t)(uintptr_t)&add3, add3_address, sizeof(add3_address), 0) == 0) {
        (void)dllsmoke_write_value("dllsmoke.exe: add3 address=", add3_address);
    }

    // write my main address
    char main_address[17];
    if (u64_to_hex((uint64_t)(uintptr_t)&main, main_address, sizeof(main_address), 0) == 0) {
        (void)dllsmoke_write_value("dllsmoke.exe: main address=", main_address);
    }

    sleepMs(200000);

    (void)dllsmoke_write_line("dllsmoke.exe: success");
    return 0;
}
