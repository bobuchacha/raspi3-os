#include "arch/aarch64/exception_disasm.h"

#include <stdarg.h>

#include "types.h"

static Size text_length(const char* text) {
    Size length = 0U;

    if (text == NULL) {
        return 0U;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static int text_equals(const char* lhs, const char* rhs) {
    Size index = 0U;

    if (lhs == rhs) {
        return 1;
    }
    if ((lhs == NULL) || (rhs == NULL)) {
        return 0;
    }

    while ((lhs[index] != '\0') && (rhs[index] != '\0')) {
        if (lhs[index] != rhs[index]) {
            return 0;
        }
        ++index;
    }

    return (lhs[index] == rhs[index]) ? 1 : 0;
}

static int has_format_specifier(const char* format) {
    Size index = 0U;

    if (format == NULL) {
        return 0;
    }

    while (format[index] != '\0') {
        if ((format[index] == '%') && (format[index + 1U] != '\0')) {
            return 1;
        }
        ++index;
    }

    return 0;
}

static const char* normalize_format_string(const char* format) {
    if (format == NULL) {
        return "";
    }
    if (text_equals(format, "#0x02x%02x")) {
        return "#0x%02x%02x";
    }
    if (text_equals(format, "#0x02x%02x0000")) {
        return "#0x%02x%02x0000";
    }
    if (text_equals(format, "#0x02x%02x%06x")) {
        return "#0x%02x%02x%06x";
    }

    return format;
}

static int unsigned_to_text(U64 value, unsigned int base, int uppercase, char* buffer) {
    char scratch[32];
    int count = 0;
    int index;

    if ((buffer == NULL) || (base < 2U) || (base > 16U)) {
        return 0;
    }

    do {
        const U64 digit = value % base;

        scratch[count++] = (digit < 10U)
            ? (char)('0' + digit)
            : (char)((uppercase ? 'A' : 'a') + (digit - 10U));
        value /= base;
    } while (value != 0U);

    for (index = 0; index < count; ++index) {
        buffer[index] = scratch[count - index - 1];
    }
    buffer[count] = '\0';
    return count;
}

static int signed_to_text(I64 value, char* buffer) {
    if (buffer == NULL) {
        return 0;
    }

    if (value < 0) {
        buffer[0] = '-';
        return 1 + unsigned_to_text((U64)(0U - (U64)value), 10U, 0, buffer + 1);
    }

    return unsigned_to_text((U64)value, 10U, 0, buffer);
}

static int format_into(char* destination, const char* format, va_list arguments) {
    char* out = destination;
    const char* cursor = normalize_format_string(format);

    if (out == NULL) {
        return 0;
    }

    while (*cursor != '\0') {
        char temp[64];
        int width = 0;
        int zero_pad = 0;
        int is_long = 0;
        int length = 0;
        int index;
        char specifier;

        if (*cursor != '%') {
            *out++ = *cursor++;
            continue;
        }

        ++cursor;
        if (*cursor == '%') {
            *out++ = *cursor++;
            continue;
        }

        if (*cursor == '0') {
            zero_pad = 1;
            ++cursor;
        }

        while ((*cursor >= '0') && (*cursor <= '9')) {
            width = (width * 10) + (*cursor - '0');
            ++cursor;
        }

        while (*cursor == 'l') {
            is_long = 1;
            ++cursor;
        }

        specifier = *cursor++;
        temp[0] = '\0';

        switch (specifier) {
        case 'c':
            temp[0] = (char)va_arg(arguments, int);
            temp[1] = '\0';
            length = 1;
            break;
        case 's': {
            const char* text = va_arg(arguments, const char*);

            if (text == NULL) {
                text = "?";
            }

            length = (int)text_length(text);
            for (index = 0; index < length; ++index) {
                temp[index] = text[index];
            }
            temp[length] = '\0';
            break;
        }
        case 'd':
        case 'i':
            length = is_long
                ? signed_to_text((I64)va_arg(arguments, long), temp)
                : signed_to_text((I64)va_arg(arguments, int), temp);
            break;
        case 'u':
            length = is_long
                ? unsigned_to_text((U64)va_arg(arguments, unsigned long), 10U, 0, temp)
                : unsigned_to_text((U64)va_arg(arguments, unsigned int), 10U, 0, temp);
            break;
        case 'x':
            length = is_long
                ? unsigned_to_text((U64)va_arg(arguments, unsigned long), 16U, 0, temp)
                : unsigned_to_text((U64)va_arg(arguments, unsigned int), 16U, 0, temp);
            break;
        case 'X':
            length = is_long
                ? unsigned_to_text((U64)va_arg(arguments, unsigned long), 16U, 1, temp)
                : unsigned_to_text((U64)va_arg(arguments, unsigned int), 16U, 1, temp);
            break;
        default:
            temp[0] = '%';
            temp[1] = specifier;
            temp[2] = '\0';
            length = 2;
            break;
        }

        if ((width > length) && zero_pad && ((specifier == 'd') || (specifier == 'i')) && (temp[0] == '-')) {
            *out++ = '-';
            for (index = 0; index < (width - length); ++index) {
                *out++ = '0';
            }
            for (index = 1; index < length; ++index) {
                *out++ = temp[index];
            }
            continue;
        }

        for (index = 0; index < (width - length); ++index) {
            *out++ = zero_pad ? '0' : ' ';
        }
        for (index = 0; index < length; ++index) {
            *out++ = temp[index];
        }
    }

    *out = '\0';
    return (int)(out - destination);
}

static int disasm_sprintf(char* destination, const char* format, ...) {
    va_list arguments;
    int written;

    va_start(arguments, format);
    format = normalize_format_string(format);
    if (text_equals(format, "%s")) {
        const char* nested_format = normalize_format_string(va_arg(arguments, const char*));

        if (has_format_specifier(nested_format)) {
            written = format_into(destination, nested_format, arguments);
        }
        else {
            Size index = 0U;

            if (nested_format == NULL) {
                nested_format = "?";
            }

            while (nested_format[index] != '\0') {
                destination[index] = nested_format[index];
                ++index;
            }
            destination[index] = '\0';
            written = (int)index;
        }
    }
    else {
        written = format_into(destination, format, arguments);
    }
    va_end(arguments);
    return written;
}

#define sprintf disasm_sprintf
#include "../../../.old/kernel/include/arch/cortex-a53/disasm.h"
#undef sprintf

VirtAddr aarch64_exception_disasm(VirtAddr address, char* buffer, Size capacity) {
    char text[160];
    VirtAddr next_address;
    Size index = 0U;

    if ((buffer == NULL) || (capacity == 0U)) {
        return address;
    }

    text[0] = '\0';
    next_address = disasm(address, text);
    while ((text[index] != '\0') && ((index + 1U) < capacity)) {
        buffer[index] = text[index];
        ++index;
    }
    buffer[index] = '\0';
    return next_address;
}