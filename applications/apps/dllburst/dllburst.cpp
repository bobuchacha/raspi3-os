#include "app/app.h"
#include <stdlib.h>

namespace {

    constexpr unsigned long kDllburstDefaultCount = 100UL;
    constexpr unsigned long kDllburstMaxCount = 256UL;
    constexpr unsigned long kDllburstPathDigits = 3UL;

    typedef long (*dllburst_add3_fn)(long lhs, long rhs, long extra);
    typedef unsigned long (*dllburst_invocation_count_fn)(void);
    typedef const char* (*dllburst_profile_fn)(void);

    /*
     * dllburst_write_line
     *
     * Route every status update through the same console helper so the stress
     * log stays easy to grep when QEMU is running headless.
     *
     * @param text Null-terminated line to emit.
     * @return Kernel write status.
     */
    static long dllburst_write_line(const char* text) {
        return writeLine(text);
    }

    /*
     * dllburst_write_unsigned
     *
     * Print one unsigned diagnostic value without depending on hosted libc
     * formatting in the freestanding userspace runtime.
     *
     * @param label Prefix text written before the numeric value.
     * @param value Unsigned value to append.
     * @return Kernel write status.
     */
    static long dllburst_write_unsigned(const char* label, unsigned long value) {
        char line[192];
        char* cursor = line;

        cursor = appendText(cursor, label);
        cursor = appendUnsignedLong(cursor, value);
        *cursor = '\0';
        return dllburst_write_line(line);
    }

    /*
     * dllburst_write_path_failure
     *
     * Attach both the failing library path and status code to one log line so
     * loader ceilings can be correlated with the exact ordinal that failed.
     *
     * @param prefix Human-readable failure stage prefix.
     * @param path DLL path involved in the failure.
     * @param status Negative runtime status code.
     * @return Kernel write status.
     */
    static long dllburst_write_path_failure(const char* prefix, const char* path, long status) {
        char line[256];
        char* cursor = line;

        cursor = appendText(cursor, prefix);
        cursor = appendText(cursor, path ? path : "<null>");
        cursor = appendText(cursor, " status=");
        if (status < 0) {
            *cursor++ = '-';
            cursor = appendUnsignedLong(cursor, static_cast<unsigned long>(-status));
        }
        else {
            cursor = appendUnsignedLong(cursor, static_cast<unsigned long>(status));
        }
        *cursor = '\0';
        return dllburst_write_line(line);
    }

    /*
     * dllburst_parse_count
     *
     * The shell forwards launch arguments as one flat string. This parser keeps
     * the stress app tolerant of empty input while still rejecting obviously bad
     * counts that would make the result ambiguous.
     *
     * @param text Raw task-argument string.
     * @return Requested DLL count, clamped to the supported range.
     */
    static unsigned long dllburst_parse_count(const char* text) {
        unsigned long value = 0UL;
        unsigned long index = 0UL;

        if (!text) {
            return kDllburstDefaultCount;
        }

        while (text[index] == ' ' || text[index] == '\t') {
            ++index;
        }
        if (text[index] < '0' || text[index] > '9') {
            return kDllburstDefaultCount;
        }

        while (text[index] >= '0' && text[index] <= '9') {
            value = (value * 10UL) + static_cast<unsigned long>(text[index] - '0');
            ++index;
        }
        if (value == 0UL) {
            return kDllburstDefaultCount;
        }
        if (value > kDllburstMaxCount) {
            return kDllburstMaxCount;
        }

        return value;
    }

    /*
     * dllburst_format_library_path
     *
     * Each DLL must have a distinct normalized path or the loader will legally
     * reuse an already-loaded image and the ceiling test becomes meaningless.
     *
     * @param index Zero-based DLL ordinal.
     * @param buffer Caller-owned path buffer.
     * @param buffer_size Size of the destination buffer in bytes.
     * @return Non-zero on success, zero when the buffer was too small.
     */
    static int dllburst_format_library_path(unsigned long index, char* buffer, unsigned long buffer_size) {
        char* cursor = buffer;
        unsigned long divisor = 100UL;
        unsigned long digit_index;

        if (!buffer || buffer_size < 24UL) {
            return 0;
        }

        cursor = appendText(cursor, "/lib/samplemath_");
        for (digit_index = 0UL; digit_index < kDllburstPathDigits; ++digit_index) {
            *cursor++ = static_cast<char>('0' + ((index / divisor) % 10UL));
            divisor /= 10UL;
        }
        cursor = appendText(cursor, ".dll");
        *cursor = '\0';
        return static_cast<unsigned long>(cursor - buffer) < buffer_size;
    }

    /*
     * dllburst_release_modules
     *
     * Release modules in reverse load order so detach notifications observe the
     * same LIFO teardown pattern as normal dependency unwinding.
     *
     * @param modules Caller-owned module array.
     * @param count Number of valid entries in the array.
     * @return Zero on success, or a negative runtime status code.
     */
    static long dllburst_release_modules(HMODULE* modules, unsigned long count) {
        long index;

        if (!modules) {
            return 0L;
        }

        for (index = static_cast<long>(count) - 1L; index >= 0L; --index) {
            if (modules[index] == 0) {
                continue;
            }
            if (freeLibrary(modules[index]) < 0L) {
                return -1L;
            }
            modules[index] = 0;
        }

        return 0L;
    }

    /*
     * dllburst_load_and_probe_one
     *
     * A loader ceiling test is only useful if each mapped image is callable and
     * owns distinct mutable state. Calling into the DLL and checking that its
     * first invocation count starts at one proves both conditions.
     *
     * @param path Distinct DLL path to load.
     * @param module Receives the loaded module handle on success.
     * @param ordinal Human-readable one-based index for log messages.
     * @return Zero on success, or a negative status code on failure.
     */
    static long dllburst_load_and_probe_one(const char* path, HMODULE* module, unsigned long ordinal) {
        dllburst_add3_fn add3;
        dllburst_invocation_count_fn invocation_count;
        dllburst_profile_fn profile;
        long total;
        unsigned long count;
        const char* profile_text;

        if (!path || !module) {
            return -1L;
        }

        *module = loadLibrary(path);
        if (*module == 0) {
            return -2L;
        }

        add3 = reinterpret_cast<dllburst_add3_fn>(getProcAddress(*module, "SampleMathAdd3"));
        invocation_count = reinterpret_cast<dllburst_invocation_count_fn>(getProcAddress(*module, "SampleMathInvocationCount"));
        profile = reinterpret_cast<dllburst_profile_fn>(getProcAddress(*module, "SampleMathProfile"));
        if (!add3 || !invocation_count || !profile) {
            return -3L;
        }

        profile_text = profile();
        if (!profile_text || profile_text[0] == '\0') {
            return -4L;
        }

        total = add3(static_cast<long>(ordinal), 10L, 20L);
        count = invocation_count();
        if (total != static_cast<long>(ordinal + 31UL) || count != 1UL) {
            return -5L;
        }

        return 0L;
    }
}

/*
 * main
 *
 * Load a caller-selected number of distinct DLL paths, keep them resident at
 * the same time, verify one exported function from each, and then unload every
 * image again. This directly tests whether the userspace DLL loader still has
 * a fixed live-module ceiling.
 *
 * @return Zero on success, or non-zero on failure.
 */
int main(void) {
    char args[64] = { 0 };
    char path[32];
    unsigned long target_count;
    unsigned long index;
    HMODULE* modules;
    long status;

    if (getTaskArgs(args, sizeof(args)) < 0) {
        args[0] = '\0';
    }

    target_count = dllburst_parse_count(args);
    (void)dllburst_write_line("dllburst.exe: starting many-dll stress");
    (void)dllburst_write_unsigned("dllburst.exe: requested count=", target_count);

    modules = static_cast<HMODULE*>(malloc(sizeof(HMODULE) * target_count));
    if (!modules) {
        (void)dllburst_write_line("dllburst.exe: failed to allocate module table");
        return 1;
    }

    for (index = 0UL; index < target_count; ++index) {
        modules[index] = 0;
    }

    for (index = 0UL; index < target_count; ++index) {
        if (!dllburst_format_library_path(index, path, sizeof(path))) {
            (void)dllburst_write_line("dllburst.exe: failed to format library path");
            (void)dllburst_release_modules(modules, index);
            free(modules);
            return 2;
        }

        status = dllburst_load_and_probe_one(path, &modules[index], index + 1UL);
        if (status != 0L) {
            (void)dllburst_write_path_failure("dllburst.exe: load failed path=", path, status);
            (void)dllburst_release_modules(modules, index + 1UL);
            free(modules);
            return 3;
        }
    }

    (void)dllburst_write_unsigned("dllburst.exe: loaded modules=", target_count);
    status = dllburst_release_modules(modules, target_count);
    free(modules);
    if (status != 0L) {
        (void)dllburst_write_line("dllburst.exe: unload failed");
        return 4;
    }

    (void)dllburst_write_line("dllburst.exe: success");
    return 0;
}