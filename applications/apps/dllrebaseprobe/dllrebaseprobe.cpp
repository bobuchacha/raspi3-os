#include "app/app.h"

namespace {

    constexpr unsigned long kDllrebaseMaxPreloadCount = 8UL;
    constexpr unsigned long kDllrebasePathDigits = 3UL;

    typedef long (*dllrebase_add3_fn)(long lhs, long rhs, long extra);

    /*
     * dllrebaseprobe_write_line
     *
     * Keep every status message on the same tiny console path so the headless
     * test log stays easy to grep.
     *
     * @param text Null-terminated line to emit.
     * @return Kernel write status.
     */
    static long dllrebaseprobe_write_line(const char* text) {
        return writeLine(text);
    }

    /*
     * dllrebaseprobe_write_unsigned
     *
     * Report one unsigned diagnostic value without depending on hosted stdio.
     *
     * @param label Prefix text written before the value.
     * @param value Unsigned value to append.
     * @return Kernel write status.
     */
    static long dllrebaseprobe_write_unsigned(const char* label, unsigned long value) {
        char line[192];
        char* cursor = line;

        cursor = appendText(cursor, label);
        cursor = appendUnsignedLong(cursor, value);
        *cursor = '\0';
        return dllrebaseprobe_write_line(line);
    }

    /*
     * dllrebaseprobe_write_handle
     *
     * The module handle is the user-visible DLL base, so printing it directly
     * lets the test compare the same library across different processes.
     *
     * @param label Prefix text written before the handle.
     * @param module Module handle returned by `loadLibrary`.
     * @return Kernel write status.
     */
    static long dllrebaseprobe_write_handle(const char* label, HMODULE module) {
        char line[192];
        char* cursor = line;

        cursor = appendText(cursor, label);
        cursor = appendText(cursor, "0x");
        cursor = appendHex(cursor, static_cast<unsigned long>(reinterpret_cast<Uptr>(module)));
        *cursor = '\0';
        return dllrebaseprobe_write_line(line);
    }

    /*
     * dllrebaseprobe_write_long
     *
     * Record the exported function result so the probe confirms the rebased DLL
     * is still callable without hard-coding one implementation-specific answer.
     *
     * @param label Prefix text written before the value.
     * @param value Signed value to append.
     * @return Kernel write status.
     */
    static long dllrebaseprobe_write_long(const char* label, long value) {
        char line[192];
        char* cursor = line;

        cursor = appendText(cursor, label);
        if (value < 0) {
            *cursor++ = '-';
            cursor = appendUnsignedLong(cursor, static_cast<unsigned long>(-value));
        }
        else {
            cursor = appendUnsignedLong(cursor, static_cast<unsigned long>(value));
        }
        *cursor = '\0';
        return dllrebaseprobe_write_line(line);
    }

    /*
     * dllrebaseprobe_parse_preload_count
     *
     * The probe only needs one integer argument: how many filler DLL aliases to
     * load before the target library. Clamping the value keeps the fixed module
     * array sufficient and makes log interpretation predictable.
     *
     * @param text Raw task-argument string.
     * @return Requested preload count, clamped to the supported range.
     */
    static unsigned long dllrebaseprobe_parse_preload_count(const char* text) {
        unsigned long value = 0UL;
        unsigned long index = 0UL;

        if (!text) {
            return 0UL;
        }

        while (text[index] == ' ' || text[index] == '\t') {
            ++index;
        }
        while (text[index] >= '0' && text[index] <= '9') {
            value = (value * 10UL) + static_cast<unsigned long>(text[index] - '0');
            ++index;
        }
        if (value > kDllrebaseMaxPreloadCount) {
            return kDllrebaseMaxPreloadCount;
        }

        return value;
    }

    /*
     * dllrebaseprobe_format_library_path
     *
     * Each filler DLL alias must use a distinct normalized path or the loader
     * will correctly reuse the same image and the target base will not move.
     *
     * @param index Zero-based filler DLL ordinal.
     * @param buffer Caller-owned path buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @return Non-zero on success, zero when the path did not fit.
     */
    static int dllrebaseprobe_format_library_path(unsigned long index, char* buffer, unsigned long buffer_size) {
        char* cursor = buffer;
        unsigned long divisor = 100UL;
        unsigned long digit_index;

        if (!buffer || buffer_size < 24UL) {
            return 0;
        }

        cursor = appendText(cursor, "/lib/samplemath_");
        for (digit_index = 0UL; digit_index < kDllrebasePathDigits; ++digit_index) {
            *cursor++ = static_cast<char>('0' + ((index / divisor) % 10UL));
            divisor /= 10UL;
        }
        cursor = appendText(cursor, ".dll");
        *cursor = '\0';
        return static_cast<unsigned long>(cursor - buffer) < buffer_size;
    }

    /*
     * dllrebaseprobe_release_modules
     *
     * Release filler DLLs in reverse load order so teardown mirrors the loader's
     * normal dependency unwinding pattern.
     *
     * @param modules Caller-owned module array.
     * @param count Number of valid entries in the array.
     * @return Zero on success, or a negative runtime status code.
     */
    static long dllrebaseprobe_release_modules(HMODULE* modules, unsigned long count) {
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
}

/*
 * main
 *
 * Load a caller-selected number of filler DLL aliases, then load the canonical
 * samplemath.dll and print its module handle. Running the same probe with
 * different preload counts demonstrates whether the loader now chooses DLL bases
 * independently for each process.
 *
 * @return Zero on success, or non-zero on failure.
 */
int main(void) {
    char args[64] = { 0 };
    char filler_path[32];
    unsigned long preload_count;
    unsigned long index;
    HMODULE filler_modules[kDllrebaseMaxPreloadCount] = { 0 };
    HMODULE target_module = 0;
    dllrebase_add3_fn add3 = 0;
    long total = 0L;

    if (getTaskArgs(args, sizeof(args)) < 0) {
        args[0] = '\0';
    }

    preload_count = dllrebaseprobe_parse_preload_count(args);
    (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: starting");
    (void)dllrebaseprobe_write_unsigned("dllrebaseprobe.exe: preload count=", preload_count);

    for (index = 0UL; index < preload_count; ++index) {
        if (!dllrebaseprobe_format_library_path(index, filler_path, sizeof(filler_path))) {
            (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: failed to format filler path");
            (void)dllrebaseprobe_release_modules(filler_modules, index);
            return 1;
        }

        filler_modules[index] = loadLibrary(filler_path);
        if (filler_modules[index] == 0) {
            (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: failed to load filler DLL");
            (void)dllrebaseprobe_release_modules(filler_modules, index);
            return 2;
        }
    }

    target_module = loadLibrary("/lib/samplemath.dll");
    if (target_module == 0) {
        (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: failed to load target DLL");
        (void)dllrebaseprobe_release_modules(filler_modules, preload_count);
        return 3;
    }

    add3 = reinterpret_cast<dllrebase_add3_fn>(getProcAddress(target_module, "SampleMathAdd3"));
    if (!add3) {
        (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: target export lookup failed");
        (void)freeLibrary(target_module);
        (void)dllrebaseprobe_release_modules(filler_modules, preload_count);
        return 4;
    }

    total = add3(1L, 2L, 30L);
    (void)dllrebaseprobe_write_long("dllrebaseprobe.exe: target result=", total);
    (void)dllrebaseprobe_write_handle("dllrebaseprobe.exe: target handle=", target_module);

    if (freeLibrary(target_module) < 0L) {
        (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: failed to release target DLL");
        (void)dllrebaseprobe_release_modules(filler_modules, preload_count);
        return 6;
    }
    if (dllrebaseprobe_release_modules(filler_modules, preload_count) < 0L) {
        (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: failed to release filler DLLs");
        return 7;
    }

    (void)dllrebaseprobe_write_line("dllrebaseprobe.exe: success");
    return 0;
}