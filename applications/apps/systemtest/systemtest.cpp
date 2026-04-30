#define ROS_APP_USE_WINDOW 1
#define ROS_APP_USE_GDI 1
#include "app/app.h"

#define SYSTEMTEST_APP_NAME "systemtest.exe"
#define SYSTEMTEST_CLASS_NAME "systemtest.main"
#define SYSTEMTEST_WINDOW_TITLE "systemtest"
#define SYSTEMTEST_DLL_NAME "samplemath.dll"
#define SYSTEMTEST_EXPECTED_PROFILE "dll-loader-shared-memory-v1"
#define SYSTEMTEST_ROOT_PATH "C:\\"
#define SYSTEMTEST_COM1_ALIAS_PATH "COM1:"
#define SYSTEMTEST_UART_ALIAS_PATH "UART:"
#define SYSTEMTEST_LFN_PATH "C:\\Long File Name.txt"
#define SYSTEMTEST_WRITABLE_SAMPLE_PATH "C:\\Writable Sample.txt"
#define SYSTEMTEST_TEMP_DIR_PATH "C:\\systemtest.dir"
#define SYSTEMTEST_EXPECTED_LFN_TEXT "lfn sample\n"
#define SYSTEMTEST_EXPECTED_WRITABLE_TEXT "initial writable sample\n"
#define SYSTEMTEST_CACHE_READ_ITERATIONS 64UL
#define SYSTEMTEST_TIMER_LIMIT 8UL
#define SYSTEMTEST_TIMER_MSEC 120UL
#define SYSTEMTEST_GWES_RETRY_COUNT 20UL
#define SYSTEMTEST_GWES_RETRY_MSEC 50UL
#define SYSTEMTEST_BAR_WIDTH 56UL
#define SYSTEMTEST_BAR_HEIGHT 20UL
#define SYSTEMTEST_MARGIN 24UL
#define SYSTEMTEST_HEADER_HEIGHT 24UL
#define SYSTEMTEST_BACKGROUND_COLOR 0x00131D2CUL
#define SYSTEMTEST_HEADER_COLOR 0x003B82F6UL
#define SYSTEMTEST_RED_COLOR 0x00DC2626UL
#define SYSTEMTEST_GREEN_COLOR 0x0022C55EUL
#define SYSTEMTEST_BLUE_COLOR 0x002563EBUL
#define SYSTEMTEST_LANE_COLOR 0x00334155UL
#define SYSTEMTEST_PASS_COLOR 0x0016A34AUL
#define SYSTEMTEST_FAIL_COLOR 0x00B91C1CUL

typedef unsigned long (*systemtest_invocation_count_fn)(void);
typedef const char* (*systemtest_profile_fn)(void);

DLL_IMPORT_FUNCTION(SYSTEMTEST_DLL_NAME, "SampleMathInvocationCount", systemtest_invocation_count_fn, g_systemtest_invocation_count);
DLL_IMPORT_FUNCTION(SYSTEMTEST_DLL_NAME, "SampleMathProfile", systemtest_profile_fn, g_systemtest_profile);
DECLARE(long, add3, (long first_number, long second_number, long third_number), FROM, SYSTEMTEST_DLL_NAME, "SampleMathAdd3");

static HWND g_systemtest_window = 0UL;
static unsigned long g_systemtest_timer_id = 0UL;
static RosGdiSurface g_systemtest_surface;
static int g_systemtest_surface_ready = 0;
static unsigned long g_systemtest_timer_count = 0UL;
static long g_systemtest_failure_count = 0L;
static int g_systemtest_keep_open = 0;

/*
 * Append one signed decimal value to a caller-owned output cursor.
 *
 * The EL0 test app keeps its logging self-contained so every verification line
 * can be emitted without depending on hosted libc helpers.
 *
 * @param destination Output cursor inside a writable character buffer.
 * @param value Signed value to format.
 * @return Advanced cursor positioned after the appended digits.
 */
static char* systemtest_append_long(char* destination, long value) {
    if (value < 0L) {
        *destination++ = '-';
        return appendUnsignedLong(destination, (unsigned long)(-value));
    }

    return appendUnsignedLong(destination, (unsigned long)value);
}

/*
 * Return the smaller of two unsigned values.
 *
 * @param left First value.
 * @param right Second value.
 * @return Smaller input value.
 */
static unsigned long systemtest_min_ul(unsigned long left, unsigned long right) {
    return left < right ? left : right;
}

/*
 * Measure one null-terminated string without depending on hosted libc.
 *
 * @param text String to measure.
 * @return String length in bytes, excluding the trailing terminator.
 */
static unsigned long systemtest_text_length(const char* text) {
    unsigned long length = 0UL;

    if (!text) {
        return 0UL;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

/*
 * Compare two ASCII strings for exact equality.
 *
 * @param left First string.
 * @param right Second string.
 * @return Non-zero when both strings match exactly.
 */
static int systemtest_text_equals(const char* left, const char* right) {
    unsigned long index = 0UL;

    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }

    return left[index] == right[index];
}

/*
 * Write one null-terminated status line to the console.
 *
 * @param text Null-terminated text line.
 * @return Kernel write status.
 */
static long systemtest_write_line(const char* text) {
    return writeLine(text);
}

/*
 * Report one PASS or FAIL verification outcome and update the failure count.
 *
 * A single helper keeps every check formatted consistently, which makes the
 * serial log easy to scan while the gfx window is visible.
 *
 * @param label Human-readable check description.
 * @param condition Non-zero when the check passed.
 * @return Same boolean-style result that the caller supplied.
 */
static int systemtest_expect_condition(const char* label, int condition) {
    char line[224];
    char* cursor = line;

    cursor = appendText(cursor, SYSTEMTEST_APP_NAME);
    cursor = appendText(cursor, condition ? ": PASS " : ": FAIL ");
    cursor = appendText(cursor, label ? label : "<unnamed>");
    *cursor = '\0';
    (void)systemtest_write_line(line);

    if (!condition) {
        ++g_systemtest_failure_count;
    }

    return condition;
}

/*
 * Log one unsigned value as part of the console verification trace.
 *
 * @param label Prefix text written before the number.
 * @param value Value to format.
 * @return Kernel write status.
 */
static long systemtest_log_value(const char* label, unsigned long value) {
    char line[224];
    char* cursor = line;

    cursor = appendText(cursor, SYSTEMTEST_APP_NAME);
    cursor = appendText(cursor, ": ");
    cursor = appendText(cursor, label ? label : "value=");
    cursor = appendUnsignedLong(cursor, value);
    *cursor = '\0';
    return systemtest_write_line(line);
}

/*
 * Log one signed value as part of the console verification trace.
 *
 * @param label Prefix text written before the number.
 * @param value Signed value to format.
 * @return Kernel write status.
 */
static long systemtest_log_signed_value(const char* label, long value) {
    char line[224];
    char* cursor = line;

    cursor = appendText(cursor, SYSTEMTEST_APP_NAME);
    cursor = appendText(cursor, ": ");
    cursor = appendText(cursor, label ? label : "value=");
    cursor = systemtest_append_long(cursor, value);
    *cursor = '\0';
    return systemtest_write_line(line);
}

/*
 * Log one free-form text value.
 *
 * @param label Prefix text written before the value.
 * @param value Optional text value.
 * @return Kernel write status.
 */
static long systemtest_log_text(const char* label, const char* value) {
    char line[256];
    char* cursor = line;

    cursor = appendText(cursor, SYSTEMTEST_APP_NAME);
    cursor = appendText(cursor, ": ");
    cursor = appendText(cursor, label ? label : "text=");
    cursor = appendText(cursor, value ? value : "<null>");
    *cursor = '\0';
    return systemtest_write_line(line);
}

/*
 * Parse optional task arguments.
 *
 * The default run auto-exits after the timer animation completes, while the
 * `loop` argument leaves the verified window alive for manual visual inspection.
 *
 * @return Nothing.
 */
static void systemtest_parse_args(void) {
    char args[128] = { 0 };

    if (getTaskArgs(args, sizeof(args)) < 0) {
        return;
    }

    if (systemtest_text_equals(args, "loop")) {
        g_systemtest_keep_open = 1;
    }
}

/*
 * Report whether one unsigned value is a power of two.
 *
 * @param value Candidate value.
 * @return Non-zero when the value is a power of two.
 */
static int systemtest_is_power_of_two(unsigned long value) {
    return value != 0UL && (value & (value - 1UL)) == 0UL;
}

/*
 * Encode one RGB test color into the active surface pixel format.
 *
 * GDI helpers accept RGB-style colors while the mapped surface may use the
 * `XBGR8888` transport format on some boards, and the shared-surface write
 * path always stamps an opaque top byte. The pixel verifier must mirror both
 * behaviors so on-screen checks validate the real stored bytes.
 *
 * @param pixel_format Target `ROS_KERNEL_GUI_PIXEL_FORMAT_*` constant.
 * @param color Caller-supplied RGB color value.
 * @return Encoded pixel value as stored in the surface buffer.
 */
static unsigned long systemtest_encode_color(unsigned long pixel_format, unsigned long color) {
    unsigned long encoded = 0xFF000000UL | (color & 0x00FFFFFFUL);

    if (pixel_format != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return encoded;
    }

    return 0xFF000000UL |
        ((color & 0x000000FFUL) << 16) |
        (color & 0x0000FF00UL) |
        ((color & 0x00FF0000UL) >> 16);
}

/*
 * Return a typed pointer to one pixel inside the cached surface mapping.
 *
 * @param surface Caller-visible surface mapping.
 * @param x Pixel X coordinate.
 * @param y Pixel Y coordinate.
 * @return Pointer to the target pixel, or NULL when the coordinate is invalid.
 */
static unsigned int* systemtest_pixel_pointer(const RosGdiSurface* surface, unsigned long x, unsigned long y) {
    unsigned char* row;

    if (!surface || !surface->pixels) {
        return NULL;
    }
    if (x >= surface->width || y >= surface->height) {
        return NULL;
    }

    row = (unsigned char*)surface->pixels + (y * surface->pitch);
    return ((unsigned int*)row) + x;
}

/*
 * Verify one stored pixel against the expected test color.
 *
 * @param label Human-readable check description.
 * @param surface Cached GDI surface.
 * @param x Pixel X coordinate.
 * @param y Pixel Y coordinate.
 * @param color Expected RGB color value.
 * @return Non-zero when the stored pixel matches the expectation.
 */
static int systemtest_expect_pixel(const char* label, const RosGdiSurface* surface, unsigned long x, unsigned long y, unsigned long color) {
    unsigned int* pixel = systemtest_pixel_pointer(surface, x, y);
    unsigned long expected = systemtest_encode_color(surface ? surface->pixel_format : 0UL, color);
    int condition = pixel != NULL && *pixel == (unsigned int)expected;

    if (!condition) {
        char line[256];
        char* cursor = line;

        cursor = appendText(cursor, SYSTEMTEST_APP_NAME);
        cursor = appendText(cursor, ": pixel mismatch ");
        cursor = appendText(cursor, label ? label : "<unnamed>");
        cursor = appendText(cursor, " expected=0x");
        cursor = appendHex(cursor, expected);
        cursor = appendText(cursor, " actual=0x");
        cursor = appendHex(cursor, pixel ? (unsigned long)(*pixel) : 0UL);
        *cursor = '\0';
        (void)systemtest_write_line(line);
    }

    return systemtest_expect_condition(label, condition);
}

/*
 * Read one file and verify its exact contents.
 *
 * The staged FAT image contains known text fixtures. Validating their exact
 * bytes proves the VFS lookup, long-file-name handling, and file read path all
 * work from a normal user process.
 *
 * @param path Absolute VFS path.
 * @param expected Exact expected file content.
 * @param label Human-readable check label.
 * @return Non-zero when the content matches exactly.
 */
static int systemtest_verify_file_content(const char* path, const char* expected, const char* label) {
    char buffer[128];
    long status;
    unsigned long expected_length = systemtest_text_length(expected);

    status = readFile(path, 0UL, buffer, sizeof(buffer) - 1UL);
    if (status < 0) {
        (void)systemtest_log_signed_value("file read status=", status);
        return systemtest_expect_condition(label, 0);
    }

    buffer[(unsigned long)status] = '\0';
    if (!systemtest_expect_condition(label, (unsigned long)status == expected_length && systemtest_text_equals(buffer, expected))) {
        (void)systemtest_log_text("file actual=", buffer);
        return 0;
    }

    return 1;
}

/*
 * Re-read one staged file many times and verify every read returns identical bytes.
 *
 * The kernel VFS cache is keyed by volume and inode, so repeatedly opening the
 * same small staged fixture is the simplest userspace-visible way to stress the
 * post-registry cache metadata path without adding a dedicated cache-inspection
 * syscall just for test code.
 *
 * @param path Absolute VFS path.
 * @param expected Exact expected file content.
 * @param iterations Number of repeated reads to perform.
 * @param label Human-readable check label.
 * @return Non-zero when every repeated read matches exactly.
 */
static int systemtest_verify_repeated_file_content(const char* path, const char* expected, unsigned long iterations, const char* label) {
    char buffer[128];
    unsigned long expected_length = systemtest_text_length(expected);

    if (!systemtest_expect_condition(label, iterations != 0UL)) {
        return 0;
    }

    for (unsigned long iteration = 0UL; iteration < iterations; ++iteration) {
        long status = readFile(path, 0UL, buffer, sizeof(buffer) - 1UL);

        if (status < 0) {
            (void)systemtest_log_signed_value("repeat file read status=", status);
            (void)systemtest_log_value("repeat file read iteration=", iteration);
            return systemtest_expect_condition(label, 0);
        }

        buffer[(unsigned long)status] = '\0';
        if (((unsigned long)status != expected_length) || !systemtest_text_equals(buffer, expected)) {
            (void)systemtest_log_value("repeat file read iteration=", iteration);
            (void)systemtest_log_text("repeat file actual=", buffer);
            return systemtest_expect_condition(label, 0);
        }
    }

    return 1;
}

/*
 * Verify that the known root-directory fixtures are visible through directory enumeration.
 *
 * @return Non-zero when both staged sample files are visible in the root directory.
 */
static int systemtest_verify_root_entries(void) {
    UserDirectoryEntry entry;
    unsigned long index;
    int found_lfn = 0;
    int found_writable = 0;

    for (index = 0UL; index < 32UL; ++index) {
        long status;

        status = readDirectoryEntry(SYSTEMTEST_ROOT_PATH, index, &entry);
        if (status < 0) {
            (void)systemtest_log_signed_value("dir read status=", status);
            return systemtest_expect_condition("root directory enumeration", 0);
        }
        if (status == 0) {
            break;
        }

        if (systemtest_text_equals(entry.name, "Long File Name.txt")) {
            found_lfn = 1;
        }
        if (systemtest_text_equals(entry.name, "Writable Sample.txt")) {
            found_writable = 1;
        }
    }

    return systemtest_expect_condition("root directory fixtures visible", found_lfn && found_writable);
}

/*
 * Exercise the writable VFS path by creating and removing one empty directory.
 *
 * @return Non-zero when both create and remove succeed.
 */
static int systemtest_verify_directory_mutation(void) {
    long create_status;
    long remove_status;

    (void)removePath(SYSTEMTEST_TEMP_DIR_PATH);
    create_status = makeDirectory(SYSTEMTEST_TEMP_DIR_PATH);
    remove_status = create_status >= 0 ? removePath(SYSTEMTEST_TEMP_DIR_PATH) : create_status;

    if (create_status < 0) {
        (void)systemtest_log_signed_value("mkdir status=", create_status);
    }
    if (remove_status < 0) {
        (void)systemtest_log_signed_value("remove status=", remove_status);
    }

    return systemtest_expect_condition("directory create/remove round-trip", create_status >= 0 && remove_status >= 0);
}

/*
 * Resolve one path and verify the returned userspace-visible node metadata.
 *
 * @param path Absolute or device-alias VFS path.
 * @param expected_type Expected `VfsNodeType` numeric value.
 * @param expected_backend Expected `VfsBackendKind` numeric value.
 * @param expected_volume_letter Expected DOS drive letter, or zero when unused.
 * @param expected_device_name Expected normalized device alias text, or null.
 * @param label Human-readable check label.
 * @return Non-zero when the resolved metadata matches the expectation.
 */
static int systemtest_verify_path_info(
    const char* path,
    unsigned long expected_type,
    unsigned long expected_backend,
    unsigned long expected_volume_letter,
    const char* expected_device_name,
    const char* label) {

    UserPathInfo info;
    long status;
    int condition;

    status = getPathInfo(path, &info);
    if (status < 0L) {
        (void)systemtest_log_signed_value("path info status=", status);
        (void)systemtest_log_text("path info path=", path);
        return systemtest_expect_condition(label, 0);
    }

    condition = (info.type == expected_type)
        && (info.backend_kind == expected_backend)
        && (expected_volume_letter == 0UL || info.volume_letter == expected_volume_letter)
        && (expected_device_name == 0 || systemtest_text_equals(info.device_name, expected_device_name));
    if (!condition) {
        (void)systemtest_log_value("path info type=", info.type);
        (void)systemtest_log_value("path info backend=", info.backend_kind);
        (void)systemtest_log_value("path info volume=", info.volume_letter);
        (void)systemtest_log_text("path info device=", info.device_name);
    }

    return systemtest_expect_condition(label, condition);
}

/*
 * Verify memory snapshot fields returned by the kernel service path.
 *
 * @return Nothing.
 */
static void systemtest_console_verify_memory(void) {
    UserMemInfo info;
    long status;

    status = getMemoryInfo(&info);
    if (!systemtest_expect_condition("memory snapshot syscall", status == 0L)) {
        return;
    }

    (void)systemtest_log_value("mem total=", info.total_bytes);
    (void)systemtest_log_value("mem free=", info.free_bytes);
    (void)systemtest_log_value("mem page_size=", info.page_size);
    (void)systemtest_log_value("mem free_pages=", info.free_pages);

    (void)systemtest_expect_condition("memory total non-zero", info.total_bytes != 0UL);
    (void)systemtest_expect_condition("memory free non-zero", info.free_bytes != 0UL);
    (void)systemtest_expect_condition("memory free <= total", info.free_bytes <= info.total_bytes);
    (void)systemtest_expect_condition("memory page size power-of-two", systemtest_is_power_of_two(info.page_size));
    (void)systemtest_expect_condition("memory free pages non-zero", info.free_pages != 0UL);
}

/*
 * Verify the raw display-info transport that backs the GUI service.
 *
 * @return Nothing.
 */
static void systemtest_console_verify_display(void) {
    RosKernelGuiDisplayInfo display = { 0 };
    long status;

    display.version = ROS_KERNEL_GUI_DISPLAY_INFO_VERSION;
    status = controlGui(ROS_KERNEL_GUI_CONTROL_DISPLAY_INFO, (unsigned long)&display);
    if (!systemtest_expect_condition("display info syscall", status == 0L)) {
        return;
    }

    (void)systemtest_log_value("display width=", display.width);
    (void)systemtest_log_value("display height=", display.height);
    (void)systemtest_log_value("display pitch=", display.pitch);
    (void)systemtest_log_value("display pixel_format=", display.pixel_format);

    (void)systemtest_expect_condition("display width non-zero", display.width != 0U);
    (void)systemtest_expect_condition("display height non-zero", display.height != 0U);
    (void)systemtest_expect_condition("display pitch non-zero", display.pitch != 0U);
}

/*
 * Verify known staged filesystem fixtures and one writable directory round-trip.
 *
 * @return Nothing.
 */
static void systemtest_console_verify_filesystem(void) {
    (void)systemtest_verify_path_info(SYSTEMTEST_ROOT_PATH, 1UL, 1UL, (unsigned long)'C', 0, "root path resolves as filesystem directory");
    (void)systemtest_verify_path_info(SYSTEMTEST_COM1_ALIAS_PATH, 3UL, 2UL, 0UL, "COM1", "COM1 alias resolves as device path");
    (void)systemtest_verify_path_info(SYSTEMTEST_UART_ALIAS_PATH, 3UL, 2UL, 0UL, "UART", "UART alias resolves as device path");
    (void)systemtest_verify_file_content(SYSTEMTEST_LFN_PATH, SYSTEMTEST_EXPECTED_LFN_TEXT, "long-file-name read matches fixture");
    (void)systemtest_verify_file_content(SYSTEMTEST_WRITABLE_SAMPLE_PATH, SYSTEMTEST_EXPECTED_WRITABLE_TEXT, "writable sample read matches fixture");
    (void)systemtest_verify_repeated_file_content(
        SYSTEMTEST_LFN_PATH,
        SYSTEMTEST_EXPECTED_LFN_TEXT,
        SYSTEMTEST_CACHE_READ_ITERATIONS,
        "long-file-name repeated reads stay stable");
    (void)systemtest_verify_repeated_file_content(
        SYSTEMTEST_WRITABLE_SAMPLE_PATH,
        SYSTEMTEST_EXPECTED_WRITABLE_TEXT,
        SYSTEMTEST_CACHE_READ_ITERATIONS,
        "writable sample repeated reads stay stable");
    (void)systemtest_verify_root_entries();
    (void)systemtest_verify_directory_mutation();
}

/*
 * Verify the sample DLL import path and its process-local mutable state.
 *
 * @return Nothing.
 */
static void systemtest_console_verify_dll(void) {
    const char* profile;
    unsigned long before_count;
    unsigned long after_first;
    unsigned long after_second;
    long first_total;
    long second_total;

    if (!systemtest_expect_condition("samplemath add3 import resolved", add3 != 0)) {
        return;
    }
    if (!systemtest_expect_condition("samplemath count import resolved", g_systemtest_invocation_count != 0)) {
        return;
    }
    if (!systemtest_expect_condition("samplemath profile import resolved", g_systemtest_profile != 0)) {
        return;
    }

    profile = g_systemtest_profile();
    (void)systemtest_log_text("dll profile=", profile);
    (void)systemtest_expect_condition("samplemath profile text", systemtest_text_equals(profile, SYSTEMTEST_EXPECTED_PROFILE));

    before_count = g_systemtest_invocation_count();
    first_total = add3(4L, 5L, 6L);
    after_first = g_systemtest_invocation_count();
    second_total = add3(1L, 2L, 3L);
    after_second = g_systemtest_invocation_count();

    (void)systemtest_log_value("dll count before=", before_count);
    (void)systemtest_log_signed_value("dll total[1]=", first_total);
    (void)systemtest_log_value("dll count after[1]=", after_first);
    (void)systemtest_log_signed_value("dll total[2]=", second_total);
    (void)systemtest_log_value("dll count after[2]=", after_second);

    (void)systemtest_expect_condition("samplemath count starts at zero", before_count == 0UL);
    (void)systemtest_expect_condition("samplemath first sum", first_total == 16L);
    (void)systemtest_expect_condition("samplemath first count", after_first == 1UL);
    (void)systemtest_expect_condition("samplemath second sum", second_total == 8L);
    (void)systemtest_expect_condition("samplemath second count", after_second == 2UL);
}

/*
 * Wait briefly for GWES to publish its process record before asserting on it.
 *
 * The shell can launch `systemtest` a little ahead of the GUI stack becoming
 * discoverable through the task-name lookup, so a short retry window avoids a
 * startup-order false negative while keeping the smoke deterministic.
 *
 * @return Stable GWES pid on success, or the last negative lookup result.
 */
static long systemtest_wait_for_gwes_pid(void) {
    unsigned long attempt;
    long pid = ROS_USER_IPC_STATUS_NOT_FOUND;

    for (attempt = 0UL; attempt < SYSTEMTEST_GWES_RETRY_COUNT; ++attempt) {
        pid = findUserTaskPidByName(ROS_WINDOW_SERVER_NAME);
        if (pid >= 0L) {
            return pid;
        }

        (void)sleepMs(SYSTEMTEST_GWES_RETRY_MSEC);
    }

    return pid;
}

/*
 * Verify that the userspace window server is already live before GUI work begins.
 *
 * @return Nothing.
 */
static void systemtest_console_verify_gwes(void) {
    long pid = systemtest_wait_for_gwes_pid();

    (void)systemtest_log_signed_value("gwes pid=", pid);
    (void)systemtest_expect_condition("gwes process is running", pid >= 0L);
}

/*
 * Run the console-side verification phase before any GUI activity starts.
 *
 * @return Nothing.
 */
static void systemtest_run_console_checks(void) {
    char task_name[64] = { 0 };

    if (getTaskName(task_name, sizeof(task_name)) >= 0) {
        (void)systemtest_log_text("task=", task_name);
        (void)systemtest_expect_condition("task name available", task_name[0] != '\0');
    }

    (void)systemtest_log_value("uptime_ms=", getUptimeMs());
    systemtest_console_verify_memory();
    systemtest_console_verify_display();
    systemtest_console_verify_filesystem();
    systemtest_console_verify_dll();
    systemtest_console_verify_gwes();
}

/*
 * Compute the animated test-lane bounds inside the current surface.
 *
 * @param x Receives the lane X origin.
 * @param y Receives the lane Y origin.
 * @param width Receives the lane width.
 * @param height Receives the lane height.
 * @return Nothing.
 */
static void systemtest_animation_bounds(unsigned long* x, unsigned long* y, unsigned long* width, unsigned long* height) {
    unsigned long lane_width;
    unsigned long lane_y;

    lane_width = g_systemtest_surface.width > (SYSTEMTEST_MARGIN * 2UL)
        ? g_systemtest_surface.width - (SYSTEMTEST_MARGIN * 2UL)
        : g_systemtest_surface.width;
    lane_y = g_systemtest_surface.height > (SYSTEMTEST_MARGIN + SYSTEMTEST_BAR_HEIGHT)
        ? g_systemtest_surface.height - (SYSTEMTEST_MARGIN + SYSTEMTEST_BAR_HEIGHT)
        : SYSTEMTEST_MARGIN;

    if (x) {
        *x = systemtest_min_ul(SYSTEMTEST_MARGIN, g_systemtest_surface.width);
    }
    if (y) {
        *y = systemtest_min_ul(lane_y, g_systemtest_surface.height);
    }
    if (width) {
        *width = lane_width;
    }
    if (height) {
        *height = systemtest_min_ul(SYSTEMTEST_BAR_HEIGHT, g_systemtest_surface.height);
    }
}

/*
 * Acquire and cache the shared surface mapping for the test window.
 *
 * @param hwnd Window handle associated with the surface.
 * @return Non-negative status on success, or a negative error code.
 */
static long systemtest_acquire_surface(HWND hwnd) {
    long status;

    status = GdiGetWindowSurface(hwnd, &g_systemtest_surface);
    if (status >= 0) {
        g_systemtest_surface_ready = 1;
    }
    else {
        g_systemtest_surface_ready = 0;
    }

    return status;
}

/*
 * Release the cached shared surface mapping when the test exits.
 *
 * @return Nothing.
 */
static void systemtest_release_surface(void) {
    if (!g_systemtest_surface_ready || g_systemtest_window == 0UL) {
        return;
    }

    (void)GdiReleaseWindowSurface(g_systemtest_window);
    g_systemtest_surface_ready = 0;
}

/*
 * Draw the initial static verification scene and validate a few stored pixels.
 *
 * The scene intentionally uses distinct solid-color regions so the on-screen
 * result is easy to recognize while the console verifies the underlying pixel
 * bytes in the shared surface mapping.
 *
 * @param hwnd Target window handle.
 * @return Nothing.
 */
static void systemtest_draw_static_scene(HWND hwnd) {
    unsigned long sample_y;
    unsigned long lane_x;
    unsigned long lane_y;
    unsigned long lane_width;
    unsigned long lane_height;

    if (!g_systemtest_surface_ready) {
        return;
    }

    sample_y = SYSTEMTEST_MARGIN + SYSTEMTEST_HEADER_HEIGHT + 16UL;

    (void)GdiFillSurfaceRect(&g_systemtest_surface, 0UL, 0UL, g_systemtest_surface.width, g_systemtest_surface.height, SYSTEMTEST_BACKGROUND_COLOR);
    (void)GdiFillSurfaceRect(&g_systemtest_surface, SYSTEMTEST_MARGIN, SYSTEMTEST_MARGIN, g_systemtest_surface.width > (SYSTEMTEST_MARGIN * 2UL) ? g_systemtest_surface.width - (SYSTEMTEST_MARGIN * 2UL) : g_systemtest_surface.width, SYSTEMTEST_HEADER_HEIGHT, SYSTEMTEST_HEADER_COLOR);
    (void)GdiFillSurfaceRect(&g_systemtest_surface, SYSTEMTEST_MARGIN, sample_y, 48UL, 48UL, SYSTEMTEST_RED_COLOR);
    (void)GdiFillSurfaceRect(&g_systemtest_surface, SYSTEMTEST_MARGIN + 64UL, sample_y, 48UL, 48UL, SYSTEMTEST_GREEN_COLOR);
    (void)GdiFillSurfaceRect(&g_systemtest_surface, SYSTEMTEST_MARGIN + 128UL, sample_y, 48UL, 48UL, SYSTEMTEST_BLUE_COLOR);

    systemtest_animation_bounds(&lane_x, &lane_y, &lane_width, &lane_height);
    (void)GdiFillSurfaceRect(&g_systemtest_surface, lane_x, lane_y, lane_width, lane_height, SYSTEMTEST_LANE_COLOR);

    (void)systemtest_expect_pixel("static background pixel", &g_systemtest_surface, 4UL, 4UL, SYSTEMTEST_BACKGROUND_COLOR);
    (void)systemtest_expect_pixel("static header pixel", &g_systemtest_surface, SYSTEMTEST_MARGIN + 4UL, SYSTEMTEST_MARGIN + 4UL, SYSTEMTEST_HEADER_COLOR);
    (void)systemtest_expect_pixel("static red pixel", &g_systemtest_surface, SYSTEMTEST_MARGIN + 8UL, sample_y + 8UL, SYSTEMTEST_RED_COLOR);
    (void)systemtest_expect_pixel("static green pixel", &g_systemtest_surface, SYSTEMTEST_MARGIN + 72UL, sample_y + 8UL, SYSTEMTEST_GREEN_COLOR);
    (void)systemtest_expect_pixel("static blue pixel", &g_systemtest_surface, SYSTEMTEST_MARGIN + 136UL, sample_y + 8UL, SYSTEMTEST_BLUE_COLOR);
    (void)systemtest_expect_condition("invalidate initial scene", GdiInvalidateRect(hwnd, 0UL, 0UL, g_systemtest_surface.width, g_systemtest_surface.height) >= 0L);
    (void)systemtest_write_line("systemtest.exe: expect a blue header, red/green/blue blocks, and a moving green bar in the gfx window");
}

/*
 * Draw one animated progress bar step and verify the written pixels.
 *
 * @param hwnd Target window handle.
 * @return Nothing.
 */
static void systemtest_draw_animation_step(HWND hwnd) {
    unsigned long lane_x;
    unsigned long lane_y;
    unsigned long lane_width;
    unsigned long lane_height;
    unsigned long travel;
    unsigned long bar_x;
    unsigned long verify_x;

    if (!g_systemtest_surface_ready) {
        return;
    }

    systemtest_animation_bounds(&lane_x, &lane_y, &lane_width, &lane_height);
    travel = lane_width > SYSTEMTEST_BAR_WIDTH ? lane_width - SYSTEMTEST_BAR_WIDTH : 0UL;
    bar_x = lane_x + ((g_systemtest_timer_count * 29UL) % (travel + 1UL));
    verify_x = bar_x + (SYSTEMTEST_BAR_WIDTH / 2UL);

    (void)systemtest_expect_condition("clear animation lane", GdiFillRect(hwnd, lane_x, lane_y, lane_width, lane_height, SYSTEMTEST_LANE_COLOR) >= 0L);
    (void)systemtest_expect_condition("draw animation bar", GdiFillRect(hwnd, bar_x, lane_y, SYSTEMTEST_BAR_WIDTH, lane_height, SYSTEMTEST_GREEN_COLOR) >= 0L);
    (void)systemtest_expect_pixel("animation lane pixel", &g_systemtest_surface, lane_x + 2UL, lane_y + 2UL, SYSTEMTEST_LANE_COLOR);
    (void)systemtest_expect_pixel("animation bar pixel", &g_systemtest_surface, verify_x, lane_y + (lane_height / 2UL), SYSTEMTEST_GREEN_COLOR);
}

/*
 * Draw one final pass/fail banner after the timer animation completes.
 *
 * @param hwnd Target window handle.
 * @return Nothing.
 */
static void systemtest_draw_completion_banner(HWND hwnd) {
    unsigned long banner_width;
    unsigned long banner_x;
    unsigned long banner_color;

    if (!g_systemtest_surface_ready) {
        return;
    }

    banner_width = g_systemtest_surface.width > (SYSTEMTEST_MARGIN * 2UL)
        ? g_systemtest_surface.width - (SYSTEMTEST_MARGIN * 2UL)
        : g_systemtest_surface.width;
    banner_x = systemtest_min_ul(SYSTEMTEST_MARGIN, g_systemtest_surface.width);
    banner_color = g_systemtest_failure_count == 0L ? SYSTEMTEST_PASS_COLOR : SYSTEMTEST_FAIL_COLOR;

    (void)systemtest_expect_condition("draw completion banner", GdiFillRect(hwnd, banner_x, SYSTEMTEST_MARGIN + SYSTEMTEST_HEADER_HEIGHT + 64UL, banner_width, 24UL, banner_color) >= 0L);
    (void)systemtest_expect_pixel("completion banner pixel", &g_systemtest_surface, banner_x + 4UL, SYSTEMTEST_MARGIN + SYSTEMTEST_HEADER_HEIGHT + 68UL, banner_color);
}

/*
 * Handle one window message for the system test window.
 *
 * @param hwnd Target window handle.
 * @param message Message identifier.
 * @param wParam First message payload word.
 * @param lParam Second message payload word.
 * @return Always zero for the current test client.
 */
static LRESULT systemtest_wndproc(HWND hwnd, unsigned long message, unsigned long wParam, unsigned long lParam) {
    (void)wParam;
    (void)lParam;

    if (message == WM_CREATE) {
        if (!systemtest_expect_condition("acquire shared window surface", systemtest_acquire_surface(hwnd) >= 0L)) {
            (void)PostQuitMessage(1L);
            return 0;
        }

        (void)systemtest_log_value("surface width=", g_systemtest_surface.width);
        (void)systemtest_log_value("surface height=", g_systemtest_surface.height);
        (void)systemtest_log_value("surface pitch=", g_systemtest_surface.pitch);
        systemtest_draw_static_scene(hwnd);
        return 0;
    }

    if (message == WM_TIMER) {
        ++g_systemtest_timer_count;
        if (g_systemtest_timer_count <= SYSTEMTEST_TIMER_LIMIT) {
            systemtest_draw_animation_step(hwnd);
        }
        else if (g_systemtest_timer_count == (SYSTEMTEST_TIMER_LIMIT + 1UL)) {
            systemtest_draw_completion_banner(hwnd);
            if (!g_systemtest_keep_open) {
                (void)PostQuitMessage(g_systemtest_failure_count == 0L ? 0L : 1L);
            }
        }
        return 0;
    }

    if (message == WM_CLOSE) {
        (void)PostQuitMessage(g_systemtest_failure_count == 0L ? 0L : 1L);
        return 0;
    }

    return 0;
}

/*
 * Entry point for the end-to-end system test application.
 *
 * The app performs console-side verification first so failures are visible in
 * headless serial logs, then opens a real GUI window and validates that the
 * framebuffer-visible scene is backed by the expected shared-surface pixels.
 *
 * @return Zero when every verification passed, or a non-zero failure code.
 */
int main(void) {
    MSG message;

    systemtest_parse_args();
    (void)systemtest_write_line("systemtest.exe: starting console and gfx verification");
    systemtest_run_console_checks();

    if (!systemtest_expect_condition("register test window class", CreateWindowClass(SYSTEMTEST_CLASS_NAME, systemtest_wndproc) >= 0L)) {
        return 1;
    }

    g_systemtest_window = CreateWindow(SYSTEMTEST_CLASS_NAME, SYSTEMTEST_WINDOW_TITLE);
    if (!systemtest_expect_condition("create test window", g_systemtest_window != 0UL)) {
        return 1;
    }
    g_systemtest_timer_id = SetTimer(g_systemtest_window, 0UL, SYSTEMTEST_TIMER_MSEC);
    if (!systemtest_expect_condition("register animation timer", g_systemtest_timer_id != 0UL)) {
        return 1;
    }

    (void)systemtest_log_value("window hwnd=", (unsigned long)g_systemtest_window);
    while (GetMessage(&message)) {
        (void)TranslateMessage(&message);
        (void)DispatchMessage(&message);
    }

    if (g_systemtest_window != 0UL && g_systemtest_timer_id != 0UL) {
        (void)KillTimer(g_systemtest_window, g_systemtest_timer_id);
        g_systemtest_timer_id = 0UL;
    }
    systemtest_release_surface();
    if (g_systemtest_failure_count == 0L) {
        (void)systemtest_write_line("systemtest.exe: completed successfully");
        return 0;
    }

    (void)systemtest_log_signed_value("failure count=", g_systemtest_failure_count);
    (void)systemtest_write_line("systemtest.exe: completed with failures");
    return 1;
}
