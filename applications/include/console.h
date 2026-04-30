/*
 * console.h
 *
 * Minimal freestanding-friendly C++ console runtime for loader user apps.
 * Bind your UART/serial routines with CONSOLE_SERIAL_* macros before including
 * this header, or let it fall back to stdio in host-side tests.
 */
#ifndef MY_LOADER_INCLUDE_CONSOLE_H
#define MY_LOADER_INCLUDE_CONSOLE_H

#ifndef __cplusplus
#error "console.h requires a C++ translation unit"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(CONSOLE_USE_ROS_RUNTIME)
#if defined(LDR_MVP)
#define CONSOLE_USE_ROS_RUNTIME 1
#endif
#endif

#if defined(CONSOLE_USE_ROS_RUNTIME) && CONSOLE_USE_ROS_RUNTIME
#include "user_runtime.h"

 /*
  * Route the freestanding console helpers through the ROS userspace syscalls by
  * default when the loader builder compiles this header into one EL0 binary.
  *
  * Keeping the bridge here means applications can include `console.h` directly
  * without having to remember a second set of `CONSOLE_SERIAL_*` defines, while
  * host-side tests can still override the macros or fall back to stdio.
  */
extern "C" size_t ros_console_platform_write(const char* text, size_t length);
extern "C" int ros_console_platform_getc(void);
extern "C" void ros_console_platform_putc(int ch);

#if !defined(CONSOLE_SERIAL_WRITE)
#define CONSOLE_SERIAL_WRITE ros_console_platform_write
#endif

#if !defined(CONSOLE_SERIAL_GETC)
#define CONSOLE_SERIAL_GETC ros_console_platform_getc
#endif

#if !defined(CONSOLE_SERIAL_PUTC)
#define CONSOLE_SERIAL_PUTC ros_console_platform_putc
#endif

#if !defined(CONSOLE_NO_STDIO)
#define CONSOLE_NO_STDIO 1
#endif
#endif

#if !defined(CONSOLE_NO_STDIO)
#if defined(__has_include)
#if __has_include(<stdio.h>)
#include <stdio.h>
#define CONSOLE_HAS_STDIO 1
#endif
#else
#include <stdio.h>
#define CONSOLE_HAS_STDIO 1
#endif
#endif

#if !defined(CONSOLE_HAS_STDIO)
#define CONSOLE_HAS_STDIO 0
#endif

#if !defined(CONSOLE_BUFFER_CHARS)
#define CONSOLE_BUFFER_CHARS 512u
#endif

#if !defined(CONSOLE_NEWLINE)
#define CONSOLE_NEWLINE "\r\n"
#endif

class Console {
public:
    enum class Colors {
        Reset = 0,
        Black,
        Red,
        Green,
        Yellow,
        Blue,
        Magenta,
        Cyan,
        White,
        Gray,
        BrightBlack,
        BrightRed,
        BrightGreen,
        BrightYellow,
        BrightBlue,
        BrightMagenta,
        BrightCyan,
        BrightWhite,
        BgBlack,
        BgRed,
        BgGreen,
        BgYellow,
        BgBlue,
        BgMagenta,
        BgCyan,
        BgWhite,
        BgBrightBlack,
        BgBrightRed,
        BgBrightGreen,
        BgBrightYellow,
        BgBrightBlue,
        BgBrightMagenta,
        BgBrightCyan,
        BgBrightWhite,
        Bold,
        Dim,
        Italic,
        Underline,
        Blink,
        Inverse,
        Hidden,
        Strikethrough
    };

    static int Write(const char* format, ...) {
        int written;
        va_list args;

        va_start(args, format);
        written = VWrite(NULL, NULL, format, args, 0);
        va_end(args);
        return written;
    }

    static int Write(Colors color, const char* format, ...) {
        int written;
        va_list args;

        va_start(args, format);
        written = VWrite(Color(color), Color(Colors::Reset), format, args, 0);
        va_end(args);
        return written;
    }

    static int WriteLine(const char* format, ...) {
        int written;
        va_list args;

        va_start(args, format);
        written = VWrite(NULL, NULL, format, args, 1);
        va_end(args);
        return written;
    }

    static int WriteLine(Colors color, const char* format, ...) {
        int written;
        va_list args;

        va_start(args, format);
        written = VWrite(Color(color), Color(Colors::Reset), format, args, 1);
        va_end(args);
        return written;
    }

    static size_t Read(char* buffer, size_t capacity) {
        size_t length = 0;

        if (!buffer || !capacity) {
            return 0;
        }

        buffer[0] = 0;

#if defined(CONSOLE_SERIAL_READ)
        length = (size_t)CONSOLE_SERIAL_READ(buffer, capacity - 1u);
        if (length >= capacity) {
            length = capacity - 1u;
        }
        buffer[length] = 0;
        return TrimLineEnd(buffer, length);
#elif defined(CONSOLE_SERIAL_GETC)
        while (length + 1u < capacity) {
            int ch = CONSOLE_SERIAL_GETC();
            if (ch < 0) {
                break;
            }
            if ('\r' == ch) {
                continue;
            }
            if ('\n' == ch) {
                break;
            }
            buffer[length++] = (char)ch;
        }
        buffer[length] = 0;
        return length;
#elif CONSOLE_HAS_STDIO
        if (!fgets(buffer, (int)capacity, stdin)) {
            return 0;
        }
        length = StringLength(buffer);
        return TrimLineEnd(buffer, length);
#else
        (void)capacity;
        return 0;
#endif
    }

    static int ReadC(void) {
#if defined(CONSOLE_SERIAL_GETC)
        return CONSOLE_SERIAL_GETC();
#elif CONSOLE_HAS_STDIO
        return fgetc(stdin);
#else
        return -1;
#endif
    }

    static void PutC(int ch) {
#if defined(CONSOLE_SERIAL_PUTC)
        CONSOLE_SERIAL_PUTC(ch);
#elif defined(CONSOLE_SERIAL_WRITE)
        char buffer[1];
        buffer[0] = (char)ch;
        CONSOLE_SERIAL_WRITE(buffer, 1u);
#elif CONSOLE_HAS_STDIO
        fputc(ch, stdout);
        fflush(stdout);
#else
        (void)ch;
#endif
    }

    static const char* Color(Colors color) {
        switch (color) {
        case Colors::Reset:
            return "\x1b[0m";
        case Colors::Black:
            return "\x1b[30m";
        case Colors::Red:
            return "\x1b[31m";
        case Colors::Green:
            return "\x1b[32m";
        case Colors::Yellow:
            return "\x1b[33m";
        case Colors::Blue:
            return "\x1b[34m";
        case Colors::Magenta:
            return "\x1b[35m";
        case Colors::Cyan:
            return "\x1b[36m";
        case Colors::White:
            return "\x1b[37m";
        case Colors::Gray:
            return "\x1b[90m";
        case Colors::BrightBlack:
            return "\x1b[90m";
        case Colors::BrightRed:
            return "\x1b[91m";
        case Colors::BrightGreen:
            return "\x1b[92m";
        case Colors::BrightYellow:
            return "\x1b[93m";
        case Colors::BrightBlue:
            return "\x1b[94m";
        case Colors::BrightMagenta:
            return "\x1b[95m";
        case Colors::BrightCyan:
            return "\x1b[96m";
        case Colors::BrightWhite:
            return "\x1b[97m";
        case Colors::BgBlack:
            return "\x1b[40m";
        case Colors::BgRed:
            return "\x1b[41m";
        case Colors::BgGreen:
            return "\x1b[42m";
        case Colors::BgYellow:
            return "\x1b[43m";
        case Colors::BgBlue:
            return "\x1b[44m";
        case Colors::BgMagenta:
            return "\x1b[45m";
        case Colors::BgCyan:
            return "\x1b[46m";
        case Colors::BgWhite:
            return "\x1b[47m";
        case Colors::BgBrightBlack:
            return "\x1b[100m";
        case Colors::BgBrightRed:
            return "\x1b[101m";
        case Colors::BgBrightGreen:
            return "\x1b[102m";
        case Colors::BgBrightYellow:
            return "\x1b[103m";
        case Colors::BgBrightBlue:
            return "\x1b[104m";
        case Colors::BgBrightMagenta:
            return "\x1b[105m";
        case Colors::BgBrightCyan:
            return "\x1b[106m";
        case Colors::BgBrightWhite:
            return "\x1b[107m";
        case Colors::Bold:
            return "\x1b[1m";
        case Colors::Dim:
            return "\x1b[2m";
        case Colors::Italic:
            return "\x1b[3m";
        case Colors::Underline:
            return "\x1b[4m";
        case Colors::Blink:
            return "\x1b[5m";
        case Colors::Inverse:
            return "\x1b[7m";
        case Colors::Hidden:
            return "\x1b[8m";
        case Colors::Strikethrough:
            return "\x1b[9m";
        default:
            return "";
        }
    }

private:
    struct FormatSpec {
        unsigned width;
        int precision;
        int leftJustify;
        int zeroPad;
        int alternate;
        int forceSign;
        int spaceSign;
        int uppercase;
        int lengthCode;
        char type;
    };

    static size_t StringLength(const char* text) {
        size_t length = 0;

        if (!text) {
            return 0;
        }

        while (text[length]) {
            ++length;
        }

        return length;
    }

    static size_t StringLengthN(const char* text, size_t maxLength) {
        size_t length = 0;

        if (!text) {
            return 0;
        }

        while (text[length] && (length < maxLength)) {
            ++length;
        }

        return length;
    }

    static size_t TrimLineEnd(char* buffer, size_t length) {
        while (length && ((buffer[length - 1u] == '\n') || (buffer[length - 1u] == '\r'))) {
            buffer[length - 1u] = 0;
            --length;
        }

        return length;
    }

    static size_t WriteBuffer(const char* text, size_t length) {
        if (!text || !length) {
            return 0;
        }

#if defined(CONSOLE_SERIAL_WRITE)
        CONSOLE_SERIAL_WRITE(text, length);
        return length;
#elif CONSOLE_HAS_STDIO
        size_t written = fwrite(text, 1u, length, stdout);
        fflush(stdout);
        return written;
#else
        (void)text;
        return 0;
#endif
    }

    static void AppendChar(char* buffer, size_t capacity, size_t* offset, size_t* total, char ch) {
        if ((*offset + 1u) < capacity) {
            buffer[*offset] = ch;
            ++(*offset);
            buffer[*offset] = 0;
        }

        ++(*total);
    }

    static void AppendRepeat(char* buffer, size_t capacity, size_t* offset, size_t* total, char ch, size_t count) {
        size_t index;

        for (index = 0; index < count; ++index) {
            AppendChar(buffer, capacity, offset, total, ch);
        }
    }

    static void AppendText(char* buffer, size_t capacity, size_t* offset, size_t* total, const char* text, size_t length) {
        size_t index;

        for (index = 0; index < length; ++index) {
            AppendChar(buffer, capacity, offset, total, text[index]);
        }
    }

    static void AppendFormattedNumber(
        char* buffer,
        size_t capacity,
        size_t* offset,
        size_t* total,
        unsigned long long value,
        unsigned base,
        const FormatSpec& spec,
        int negative,
        const char* forcedPrefix) {
        char digits[32];
        char prefix[4];
        const char* alphabet = spec.uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        size_t digitCount = 0;
        size_t prefixLength = 0;
        size_t zeroCount = 0;
        size_t contentWidth;
        size_t padCount = 0;
        char padChar = ' ';

        if (!base) {
            return;
        }

        if ((0 == value) && (0 == spec.precision) && ('p' != spec.type)) {
            digitCount = 0;
        }
        else {
            do {
                digits[digitCount++] = alphabet[value % base];
                value /= base;
            } while (value && (digitCount < sizeof(digits)));
        }

        if (negative) {
            prefix[prefixLength++] = '-';
        }
        else if (spec.forceSign) {
            prefix[prefixLength++] = '+';
        }
        else if (spec.spaceSign) {
            prefix[prefixLength++] = ' ';
        }

        if (forcedPrefix) {
            while (*forcedPrefix && (prefixLength < sizeof(prefix))) {
                prefix[prefixLength++] = *forcedPrefix++;
            }
        }
        else if (spec.alternate && (16u == base) && digitCount) {
            prefix[prefixLength++] = '0';
            prefix[prefixLength++] = spec.uppercase ? 'X' : 'x';
        }

        if ((spec.precision > 0) && ((size_t)spec.precision > digitCount)) {
            zeroCount = (size_t)spec.precision - digitCount;
        }

        contentWidth = prefixLength + zeroCount + digitCount;
        if ((spec.width > contentWidth) && !spec.leftJustify) {
            padCount = spec.width - contentWidth;
        }

        if (spec.zeroPad && !spec.leftJustify && (spec.precision < 0)) {
            padChar = '0';
        }

        if ('0' == padChar) {
            AppendText(buffer, capacity, offset, total, prefix, prefixLength);
            AppendRepeat(buffer, capacity, offset, total, '0', padCount + zeroCount);
        }
        else {
            AppendRepeat(buffer, capacity, offset, total, ' ', padCount);
            AppendText(buffer, capacity, offset, total, prefix, prefixLength);
            AppendRepeat(buffer, capacity, offset, total, '0', zeroCount);
        }

        while (digitCount) {
            AppendChar(buffer, capacity, offset, total, digits[digitCount - 1u]);
            --digitCount;
        }

        if ((spec.width > contentWidth) && spec.leftJustify) {
            AppendRepeat(buffer, capacity, offset, total, ' ', spec.width - contentWidth);
        }
    }

    static int ParseNumber(const char** text) {
        int value = 0;

        while (**text && (**text >= '0') && (**text <= '9')) {
            value = (value * 10) + (**text - '0');
            ++(*text);
        }

        return value;
    }

    static int FormatV(char* buffer, size_t capacity, const char* format, va_list args) {
        size_t offset = 0;
        size_t total = 0;

        if (!buffer || !capacity) {
            return 0;
        }

        buffer[0] = 0;

        while (format && *format) {
            FormatSpec spec = {};

            if ('%' != *format) {
                AppendChar(buffer, capacity, &offset, &total, *format++);
                continue;
            }

            ++format;
            if ('%' == *format) {
                AppendChar(buffer, capacity, &offset, &total, *format++);
                continue;
            }

            spec.precision = -1;

            for (;;) {
                if ('-' == *format) {
                    spec.leftJustify = 1;
                }
                else if ('0' == *format) {
                    spec.zeroPad = 1;
                }
                else if ('#' == *format) {
                    spec.alternate = 1;
                }
                else if ('+' == *format) {
                    spec.forceSign = 1;
                }
                else if (' ' == *format) {
                    spec.spaceSign = 1;
                }
                else {
                    break;
                }

                ++format;
            }

            if ((*format >= '0') && (*format <= '9')) {
                spec.width = (unsigned)ParseNumber(&format);
            }

            if ('.' == *format) {
                ++format;
                spec.precision = ParseNumber(&format);
            }

            if ('h' == *format) {
                spec.lengthCode = 1;
                ++format;
                if ('h' == *format) {
                    ++format;
                }
            }
            else if ('l' == *format) {
                spec.lengthCode = 2;
                ++format;
                if ('l' == *format) {
                    spec.lengthCode = 3;
                    ++format;
                }
            }
            else if ('z' == *format) {
                spec.lengthCode = 4;
                ++format;
            }

            spec.type = *format;
            if (*format) {
                ++format;
            }

            switch (spec.type) {
            case 'c':
            {
                char ch = (char)va_arg(args, int);
                size_t padCount = (spec.width > 1u) ? (spec.width - 1u) : 0u;

                if (!spec.leftJustify) {
                    AppendRepeat(buffer, capacity, &offset, &total, ' ', padCount);
                }
                AppendChar(buffer, capacity, &offset, &total, ch);
                if (spec.leftJustify) {
                    AppendRepeat(buffer, capacity, &offset, &total, ' ', padCount);
                }
                break;
            }
            case 's':
            {
                const char* text = va_arg(args, const char*);
                size_t length;
                size_t padCount;

                if (!text) {
                    text = "<null>";
                }

                length = (spec.precision >= 0) ? StringLengthN(text, (size_t)spec.precision) : StringLength(text);
                padCount = (spec.width > length) ? (spec.width - length) : 0u;

                if (!spec.leftJustify) {
                    AppendRepeat(buffer, capacity, &offset, &total, ' ', padCount);
                }
                AppendText(buffer, capacity, &offset, &total, text, length);
                if (spec.leftJustify) {
                    AppendRepeat(buffer, capacity, &offset, &total, ' ', padCount);
                }
                break;
            }
            case 'd':
            case 'i':
            {
                long long signedValue;
                unsigned long long magnitude;
                int negative;

                if (4 == spec.lengthCode) {
                    signedValue = (long long)va_arg(args, ptrdiff_t);
                }
                else if (3 == spec.lengthCode) {
                    signedValue = va_arg(args, long long);
                }
                else if (2 == spec.lengthCode) {
                    signedValue = (long long)va_arg(args, long);
                }
                else {
                    signedValue = (long long)va_arg(args, int);
                }

                negative = (signedValue < 0);
                magnitude = negative ? (0ull - (unsigned long long)signedValue) : (unsigned long long)signedValue;
                AppendFormattedNumber(buffer, capacity, &offset, &total, magnitude, 10u, spec, negative, NULL);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            {
                unsigned long long value;

                spec.uppercase = ('X' == spec.type);
                if (4 == spec.lengthCode) {
                    value = (unsigned long long)va_arg(args, size_t);
                }
                else if (3 == spec.lengthCode) {
                    value = va_arg(args, unsigned long long);
                }
                else if (2 == spec.lengthCode) {
                    value = (unsigned long long)va_arg(args, unsigned long);
                }
                else {
                    value = (unsigned long long)va_arg(args, unsigned int);
                }

                AppendFormattedNumber(
                    buffer,
                    capacity,
                    &offset,
                    &total,
                    value,
                    (('x' == spec.type) || ('X' == spec.type)) ? 16u : 10u,
                    spec,
                    0,
                    NULL);
                break;
            }
            case 'p':
            {
                unsigned long long value = (unsigned long long)(uintptr_t)va_arg(args, void*);
                spec.precision = (spec.precision < 0) ? 1 : spec.precision;
                AppendFormattedNumber(buffer, capacity, &offset, &total, value, 16u, spec, 0, "0x");
                break;
            }
            default:
                AppendChar(buffer, capacity, &offset, &total, '%');
                if (spec.type) {
                    AppendChar(buffer, capacity, &offset, &total, spec.type);
                }
                break;
            }
        }

        return (int)total;
    }

    static int VWrite(const char* prefix, const char* suffix, const char* format, va_list args, int appendNewline) {
        char buffer[CONSOLE_BUFFER_CHARS];
        size_t total = 0;
        size_t length;
        int formatted;

        if (!format) {
            return 0;
        }

        buffer[0] = 0;
        formatted = FormatV(buffer, sizeof(buffer), format, args);
        if (formatted < 0) {
            buffer[0] = 0;
            formatted = 0;
        }

        if (prefix && prefix[0]) {
            total += WriteBuffer(prefix, StringLength(prefix));
        }

        length = StringLength(buffer);
        total += WriteBuffer(buffer, length);

        if (suffix && suffix[0]) {
            total += WriteBuffer(suffix, StringLength(suffix));
        }

        if (appendNewline) {
            total += WriteBuffer(CONSOLE_NEWLINE, sizeof(CONSOLE_NEWLINE) - 1u);
        }

        return (int)total;
    }
};

#endif /* MY_LOADER_INCLUDE_CONSOLE_H */