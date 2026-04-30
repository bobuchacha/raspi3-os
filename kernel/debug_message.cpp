#include "debug-message.h"

#include "kernel_time.h"
#include "scheduler.h"
#include "serial.h"

#include <stdarg.h>
#include <stddef.h>

namespace {

    inline constexpr Size KernelDebugFileFilterCapacity = 16U;
    inline constexpr Size KernelDebugFileNameCapacity = 48U;

    typedef struct KernelFormatSink {
        char* buffer;
        size_t capacity;
        size_t length;
    } KernelFormatSink;

    typedef struct KernelDebugFileFilterState {
        char enabled_files[KernelDebugFileFilterCapacity][KernelDebugFileNameCapacity];
        char disabled_files[KernelDebugFileFilterCapacity][KernelDebugFileNameCapacity];
        Size enabled_count;
        Size disabled_count;
    } KernelDebugFileFilterState;

    KernelDebugFileFilterState g_kernel_debug_file_filters = {};

    /*
     * Append one character to the formatting sink while still tracking the full
     * logical length for `vsnprintf` semantics.
     *
     * @param sink Destination sink receiving the formatted character.
     * @param ch One character to append.
     * @return Nothing.
     */
    void kernel_format_push_char(KernelFormatSink* sink, char ch) {
        if (sink == NULL) {
            return;
        }

        if ((sink->buffer != NULL) && (sink->capacity > 0U) && ((sink->length + 1U) < sink->capacity)) {
            sink->buffer[sink->length] = ch;
        }
        sink->length += 1U;
    }

    /*
     * Append one string to the formatting sink.
     *
     * @param sink Destination sink receiving the text.
     * @param text Null-terminated text, or NULL for a printable placeholder.
     * @return Nothing.
     */
    void kernel_format_push_text(KernelFormatSink* sink, const char* text) {
        if (text == NULL) {
            text = "(null)";
        }

        while (*text != '\0') {
            kernel_format_push_char(sink, *text++);
        }
    }

    /*
     * Append repeated padding characters to the formatting sink.
     *
     * @param sink Destination sink receiving the padding.
     * @param count Number of padding characters to append.
     * @param pad Padding character.
     * @return Nothing.
     */
    void kernel_format_push_padding(KernelFormatSink* sink, int count, char pad) {
        while (count-- > 0) {
            kernel_format_push_char(sink, pad);
        }
    }

    /*
     * Measure one string length without relying on the hosted C library.
     *
     * @param text Null-terminated string to measure.
     * @return Length in bytes.
     */
    size_t kernel_format_text_length(const char* text) {
        size_t length = 0U;

        if (text == NULL) {
            return 6U;
        }

        while (text[length] != '\0') {
            length += 1U;
        }

        return length;
    }

    /*
     * Compare two null-terminated strings exactly.
     *
     * The debug filter keeps short basenames only, so exact comparison is both
     * cheaper and less error-prone than adding wildcard semantics during early
     * boot logging.
     *
     * @param lhs First text pointer.
     * @param rhs Second text pointer.
     * @return `true` when both strings match exactly.
     */
    bool kernel_debug_text_equal(const char* lhs, const char* rhs) {
        if (lhs == rhs) {
            return true;
        }
        if ((lhs == NULL) || (rhs == NULL)) {
            return false;
        }

        while ((*lhs != '\0') && (*rhs != '\0')) {
            if (*lhs != *rhs) {
                return false;
            }
            ++lhs;
            ++rhs;
        }

        return (*lhs == '\0') && (*rhs == '\0');
    }

    /*
     * Copy one short source-file name into fixed storage.
     *
     * The filter state owns the copied basename so later runtime checks remain
     * valid even when callers provide transient buffers instead of literals.
     *
     * @param destination Fixed-capacity output buffer.
     * @param capacity Size of `destination` in bytes.
     * @param text Source text to copy.
     * @return `StatusOK` on success, or `StatusInvalidArgument` when the text
     * does not fit.
     */
    Status kernel_debug_copy_text(char* destination, Size capacity, const char* text) {
        Size index = 0U;

        if ((destination == NULL) || (capacity == 0U) || (text == NULL) || (*text == '\0')) {
            return StatusInvalidArgument;
        }

        while (text[index] != '\0') {
            if ((index + 1U) >= capacity) {
                destination[0] = '\0';
                return StatusInvalidArgument;
            }

            destination[index] = text[index];
            ++index;
        }

        destination[index] = '\0';
        return StatusOK;
    }

    /*
     * Report whether one basename already exists in a filter list.
     *
     * @param entries Fixed-capacity filter entry table.
     * @param count Live entry count inside `entries`.
     * @param short_name Basename to search for.
     * @return `true` when the basename is already present.
     */
    bool kernel_debug_filter_contains(
        const char entries[KernelDebugFileFilterCapacity][KernelDebugFileNameCapacity],
        Size count,
        const char* short_name) {
        for (Size index = 0U; index < count; ++index) {
            if (kernel_debug_text_equal(entries[index], short_name)) {
                return true;
            }
        }

        return false;
    }

    /*
     * Append one basename to a filter list when capacity allows.
     *
     * @param entries Fixed-capacity filter entry table.
     * @param count_in_out Live entry count to update on success.
     * @param path Basename or full source path supplied by the caller.
     * @return `StatusOK`, `StatusAlreadyExists`, `StatusNoSpace`, or
     * `StatusInvalidArgument` when normalization fails.
     */
    Status kernel_debug_filter_add(
        char entries[KernelDebugFileFilterCapacity][KernelDebugFileNameCapacity],
        Size* count_in_out,
        const char* path) {
        const char* short_name;
        Status status;

        if ((count_in_out == NULL) || (path == NULL)) {
            return StatusInvalidArgument;
        }

        short_name = kernel_debug_short_file(path);
        if ((short_name == NULL) || (*short_name == '\0')) {
            return StatusInvalidArgument;
        }
        if (kernel_debug_filter_contains(entries, *count_in_out, short_name)) {
            return StatusAlreadyExists;
        }
        if (*count_in_out >= KernelDebugFileFilterCapacity) {
            return StatusNoSpace;
        }

        status = kernel_debug_copy_text(entries[*count_in_out], KernelDebugFileNameCapacity, short_name);
        if (status == StatusOK) {
            *count_in_out += 1U;
        }

        return status;
    }

    /*
     * Convert one unsigned integer to ASCII in the requested base.
     *
     * @param buffer Caller-owned digit buffer.
     * @param value Unsigned value to encode.
     * @param base Numeric base between 2 and 16.
     * @param uppercase Non-zero when hexadecimal digits should be uppercase.
     * @return Number of digits written into `buffer`.
     */
    size_t kernel_format_unsigned(char* buffer, unsigned long long value, unsigned int base, int uppercase) {
        static const char DigitsLower[] = "0123456789abcdef";
        static const char DigitsUpper[] = "0123456789ABCDEF";
        const char* digits = uppercase ? DigitsUpper : DigitsLower;
        char scratch[32];
        size_t length = 0U;
        size_t index;

        if ((buffer == NULL) || (base < 2U) || (base > 16U)) {
            return 0U;
        }
        if (value == 0ULL) {
            buffer[0] = '0';
            return 1U;
        }

        while (value != 0ULL) {
            scratch[length++] = digits[value % base];
            value /= base;
        }

        for (index = 0U; index < length; ++index) {
            buffer[index] = scratch[length - 1U - index];
        }

        return length;
    }

    /*
     * Format one integer field with optional sign, width, zero padding, and
     * hexadecimal prefix handling.
     *
     * @param sink Destination sink receiving the formatted number.
     * @param value Unsigned value magnitude to print.
     * @param negative Non-zero when a leading minus sign is required.
     * @param base Numeric base between 2 and 16.
     * @param uppercase Non-zero for uppercase hexadecimal digits.
     * @param width Minimum field width.
     * @param zero_pad Non-zero when left padding should use zeros.
     * @param prefix_hex Non-zero when `0x` or `0X` should be emitted.
     * @return Nothing.
     */
    void kernel_format_number(
        KernelFormatSink* sink,
        unsigned long long value,
        int negative,
        unsigned int base,
        int uppercase,
        int width,
        int zero_pad,
        int prefix_hex
    ) {
        char digits[32];
        size_t length = kernel_format_unsigned(digits, value, base, uppercase);
        int prefix_length = 0;
        size_t index;

        if (negative) {
            prefix_length += 1;
        }
        if (prefix_hex) {
            prefix_length += 2;
        }

        if (!zero_pad) {
            kernel_format_push_padding(sink, width - (int)(length + (size_t)prefix_length), ' ');
        }
        if (negative) {
            kernel_format_push_char(sink, '-');
        }
        if (prefix_hex) {
            kernel_format_push_char(sink, '0');
            kernel_format_push_char(sink, uppercase ? 'X' : 'x');
        }
        if (zero_pad) {
            kernel_format_push_padding(sink, width - (int)(length + (size_t)prefix_length), '0');
        }

        for (index = 0U; index < length; ++index) {
            kernel_format_push_char(sink, digits[index]);
        }
    }

    /*
     * Provide one small freestanding formatter so the shared debug-message
     * macros can still use `vsnprintf` without pulling in libc.
     *
     * @param sink Destination sink receiving the formatted text.
     * @param fmt Printf-style format string.
     * @param args Varargs state for the format string.
     * @return Total number of bytes that would have been written.
     */
    int kernel_format_vprintf(KernelFormatSink* sink, const char* fmt, va_list args) {
        while ((fmt != NULL) && (*fmt != '\0')) {
            int zero_pad = 0;
            int width = 0;
            int long_count = 0;
            char spec;

            if (*fmt != '%') {
                kernel_format_push_char(sink, *fmt++);
                continue;
            }

            fmt += 1;
            if (*fmt == '%') {
                kernel_format_push_char(sink, *fmt++);
                continue;
            }
            if (*fmt == '0') {
                zero_pad = 1;
                fmt += 1;
            }
            while ((*fmt >= '0') && (*fmt <= '9')) {
                width = (width * 10) + (*fmt - '0');
                fmt += 1;
            }
            while ((*fmt == 'l') || (*fmt == 'z')) {
                if (*fmt == 'l') {
                    long_count += 1;
                }
                else {
                    long_count = 1;
                }
                fmt += 1;
            }

            spec = (*fmt != '\0') ? *fmt++ : '\0';
            switch (spec) {
            case '\0':
                break;
            case 'c':
                kernel_format_push_char(sink, (char)va_arg(args, int));
                break;
            case 's': {
                const char* text = va_arg(args, const char*);
                size_t length = kernel_format_text_length(text);

                kernel_format_push_padding(sink, width - (int)length, ' ');
                kernel_format_push_text(sink, text);
                break;
            }
            case 'd':
            case 'i': {
                long long signed_value;
                unsigned long long magnitude;

                if (long_count >= 2) {
                    signed_value = va_arg(args, long long);
                }
                else if (long_count == 1) {
                    signed_value = va_arg(args, long);
                }
                else {
                    signed_value = va_arg(args, int);
                }

                magnitude = (signed_value < 0) ? (unsigned long long)(-signed_value) : (unsigned long long)signed_value;
                kernel_format_number(sink, magnitude, signed_value < 0, 10U, 0, width, zero_pad, 0);
                break;
            }
            case 'u': {
                unsigned long long value;

                if (long_count >= 2) {
                    value = va_arg(args, unsigned long long);
                }
                else if (long_count == 1) {
                    value = va_arg(args, unsigned long);
                }
                else {
                    value = va_arg(args, unsigned int);
                }

                kernel_format_number(sink, value, 0, 10U, 0, width, zero_pad, 0);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long value;

                if (long_count >= 2) {
                    value = va_arg(args, unsigned long long);
                }
                else if (long_count == 1) {
                    value = va_arg(args, unsigned long);
                }
                else {
                    value = va_arg(args, unsigned int);
                }

                kernel_format_number(sink, value, 0, 16U, spec == 'X', width, zero_pad, 0);
                break;
            }
            case 'p': {
                unsigned long long value = (unsigned long long)(Uptr)va_arg(args, void*);

                kernel_format_number(sink, value, 0, 16U, 0, width, zero_pad, 1);
                break;
            }
            default:
                kernel_format_push_char(sink, '%');
                kernel_format_push_char(sink, spec);
                break;
            }
        }

        if ((sink != NULL) && (sink->buffer != NULL) && (sink->capacity > 0U)) {
            size_t terminator_index = (sink->length < sink->capacity) ? sink->length : (sink->capacity - 1U);

            sink->buffer[terminator_index] = '\0';
        }

        return (sink != NULL) ? (int)sink->length : 0;
    }

    /*
     * Emit one already-formatted debug line through the live board serial sink.
     *
     * @param context Unused sink context placeholder.
     * @param text Fully formatted text to write.
     * @return Nothing.
     */
    void kernel_debug_sink(void* context, const char* text) {
        (void)context;

        if (text == NULL) {
            return;
        }

        board::Serial::puts(text);
    }

}

extern "C" int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args) {
    KernelFormatSink sink;

    sink.buffer = buffer;
    sink.capacity = size;
    sink.length = 0U;
    return kernel_format_vprintf(&sink, fmt, args);
}

extern "C" int snprintf(char* buffer, size_t size, const char* fmt, ...) {
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return written;
}

extern "C" {

    DBGPARAM dpCurSettings = MCDBG_PARAMS_INIT_EX(
        MCDBG_TEXT("KERNEL"),
        (KERNEL_DEBUG_ENABLE ? (uint32_t)KERNEL_DEBUG_ZONE_MASK : 0u),
        0u,
        kernel_debug_sink,
        NULL,
        NULL,
        NULL,
        NULL,
        MCDBG_ZONE_NAMES_CE_KERNEL);

    const char* kernel_debug_short_file(const char* path) {
        const char* file = path;

        if (path == NULL) {
            return "<unknown>";
        }

        while (*path != '\0') {
            if ((*path == '/') || (*path == '\\')) {
                file = path + 1;
            }
            ++path;
        }

        return file;
    }

    void kernel_debug_initialize(void) {
        mcdbg_set_sink(&dpCurSettings, kernel_debug_sink, NULL);
        mcdbg_set_zone_mask(&dpCurSettings, (KERNEL_DEBUG_ENABLE ? (uint32_t)KERNEL_DEBUG_ZONE_MASK : 0u));
        kernel_debug_clear_file_filters();
    }

    void kernel_debug_set_zone_mask(U32 zone_mask) {
        mcdbg_set_zone_mask(&dpCurSettings, (uint32_t)zone_mask);
    }

    U32 kernel_debug_zone_mask(void) {
        return (U32)dpCurSettings.ulZoneMask;
    }

    /*
     * Enable one named CE-style debug zone bit without forcing callers to
     * rebuild the full mask manually.
     *
     * @param zone_bit Zero-based zone bit index to enable.
     * @return Nothing.
     */
    void kernel_debug_enable_zone(U32 zone_bit) {
        mcdbg_enable_zone(&dpCurSettings, (unsigned)zone_bit);
    }

    /*
     * Disable one named CE-style debug zone bit so noisy subsystems can be
     * muted independently while leaving other zones active.
     *
     * @param zone_bit Zero-based zone bit index to disable.
     * @return Nothing.
     */
    void kernel_debug_disable_zone(U32 zone_bit) {
        mcdbg_disable_zone(&dpCurSettings, (unsigned)zone_bit);
    }

    /*
     * Check one debug zone bit from the active mask.
     *
     * @param zone_bit Zero-based zone bit index to query.
     * @return `true` when the zone is enabled, otherwise `false`.
     */
    bool kernel_debug_is_zone_enabled(U32 zone_bit) {
        return mcdbg_zone_enabled(&dpCurSettings, (unsigned)zone_bit) ? true : false;
    }

    /*
     * Reset the source-file allow and deny lists.
     *
     * @return Nothing.
     */
    void kernel_debug_clear_file_filters(void) {
        for (Size index = 0U; index < KernelDebugFileFilterCapacity; ++index) {
            g_kernel_debug_file_filters.enabled_files[index][0] = '\0';
            g_kernel_debug_file_filters.disabled_files[index][0] = '\0';
        }

        g_kernel_debug_file_filters.enabled_count = 0U;
        g_kernel_debug_file_filters.disabled_count = 0U;
    }

    /*
     * Add one basename to the active allow list.
     *
     * @param file_name Basename or full path to store.
     * @return Status from the filter insertion helper.
     */
    Status kernel_debug_add_enabled_file(const char* file_name) {
        return kernel_debug_filter_add(
            g_kernel_debug_file_filters.enabled_files,
            &g_kernel_debug_file_filters.enabled_count,
            file_name);
    }

    /*
     * Add one basename to the active deny list.
     *
     * @param file_name Basename or full path to store.
     * @return Status from the filter insertion helper.
     */
    Status kernel_debug_add_disabled_file(const char* file_name) {
        return kernel_debug_filter_add(
            g_kernel_debug_file_filters.disabled_files,
            &g_kernel_debug_file_filters.disabled_count,
            file_name);
    }

    /*
     * Check whether one `KDEBUG` call passes both zone and file filters.
     *
     * @param zone_mask Zone mask attached to the debug call site.
     * @param path Full `__FILE__` path for the call site.
     * @return `true` when the message should be printed.
     */
    bool kernel_debug_should_emit(U32 zone_mask, const char* path) {
        const char* short_name = kernel_debug_short_file(path);

        if ((zone_mask & kernel_debug_zone_mask()) == 0U) {
            return false;
        }
        if ((g_kernel_debug_file_filters.enabled_count != 0U)
            && !kernel_debug_filter_contains(
                g_kernel_debug_file_filters.enabled_files,
                g_kernel_debug_file_filters.enabled_count,
                short_name)) {
            return false;
        }
        if (kernel_debug_filter_contains(
            g_kernel_debug_file_filters.disabled_files,
            g_kernel_debug_file_filters.disabled_count,
            short_name)) {
            return false;
        }

        return true;
    }

    /*
     * Convert the live scheduler tick count into a millisecond log prefix.
     *
     * Using the scheduler-backed uptime keeps every debug line on one shared
     * relative timeline without forcing each caller to add its own timestamp.
     * During the earliest boot window the tick count is still zero, which is a
     * safe and explicit prefix until the periodic timer starts running.
     *
     * @return Millisecond uptime used in debug-log prefixes.
     */
    U64 kernel_debug_timestamp_msec(void) {
        return KernelTime::ticks_to_milliseconds(Scheduler::tick_count());
    }

}