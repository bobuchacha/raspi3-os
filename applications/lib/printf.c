#include "app/kernel.h"
#include "stdarg.h"
#include "stddef.h"
#include "stdio.h"

typedef void (*FormatEmitFn)(void *ctx, char ch);

typedef struct BufferFormatState
{
    char *buffer;
    size_t size;
    size_t length;
} BufferFormatState;

typedef struct RawFormatState
{
    char *buffer;
    size_t length;
} RawFormatState;

static size_t app_strlen_internal(const char *text)
{
    size_t length = 0;

    if (!text)
    {
        return 0;
    }

    while (text[length] != '\0')
    {
        length++;
    }

    return length;
}

static void buffer_emit(void *ctx, char ch)
{
    BufferFormatState *state = (BufferFormatState *)ctx;

    if (state->size > 0 && state->length + 1 < state->size)
    {
        state->buffer[state->length] = ch;
    }

    state->length++;
}

static void raw_emit(void *ctx, char ch)
{
    RawFormatState *state = (RawFormatState *)ctx;
    state->buffer[state->length++] = ch;
}

static void terminate_buffer(BufferFormatState *state)
{
    size_t index;

    if (state->size == 0)
    {
        return;
    }

    index = state->length;
    if (index >= state->size)
    {
        index = state->size - 1;
    }

    state->buffer[index] = '\0';
}

static void terminate_raw(RawFormatState *state)
{
    state->buffer[state->length] = '\0';
}

static void write_padding(FormatEmitFn emit, void *ctx, int count, char fill)
{
    while (count-- > 0)
    {
        emit(ctx, fill);
    }
}

static int format_unsigned(char *buffer, unsigned long long value, unsigned int base, int uppercase)
{
    static const char digits_low[] = "0123456789abcdef";
    static const char digits_high[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_high : digits_low;
    int length = 0;

    if (value == 0)
    {
        buffer[length++] = '0';
        buffer[length] = '\0';
        return length;
    }

    while (value > 0)
    {
        buffer[length++] = digits[value % base];
        value /= base;
    }

    for (int left = 0, right = length - 1; left < right; left++, right--)
    {
        char tmp = buffer[left];
        buffer[left] = buffer[right];
        buffer[right] = tmp;
    }

    buffer[length] = '\0';
    return length;
}

static int format_signed(char *buffer, long long value)
{
    if (value < 0)
    {
        buffer[0] = '-';
        return 1 + format_unsigned(buffer + 1, (unsigned long long)(-value), 10, 0);
    }

    return format_unsigned(buffer, (unsigned long long)value, 10, 0);
}

static int app_vformat(FormatEmitFn emit, void *ctx, const char *fmt, va_list args)
{
    int total = 0;

    while (*fmt != '\0')
    {
        char ch = *fmt++;

        if (ch != '%')
        {
            emit(ctx, ch);
            total++;
            continue;
        }

        int zero_pad = 0;
        int width = 0;
        int long_count = 0;
        char buffer[32];
        const char *text = buffer;
        int text_length = 0;

        ch = *fmt++;
        if (ch == '0')
        {
            zero_pad = 1;
            ch = *fmt++;
        }
        while (ch >= '0' && ch <= '9')
        {
            width = (width * 10) + (ch - '0');
            ch = *fmt++;
        }
        while (ch == 'l')
        {
            long_count++;
            ch = *fmt++;
        }

        switch (ch)
        {
        case '\0':
            fmt--;
            continue;
        case 'u':
            if (long_count > 0)
            {
                text_length = format_unsigned(buffer, va_arg(args, unsigned long), 10, 0);
            }
            else
            {
                text_length = format_unsigned(buffer, va_arg(args, unsigned int), 10, 0);
            }
            break;
        case 'd':
        case 'i':
            if (long_count > 0)
            {
                text_length = format_signed(buffer, va_arg(args, long));
            }
            else
            {
                text_length = format_signed(buffer, va_arg(args, int));
            }
            break;
        case 'x':
        case 'X':
            if (long_count > 0)
            {
                text_length = format_unsigned(buffer, va_arg(args, unsigned long), 16, ch == 'X');
            }
            else
            {
                text_length = format_unsigned(buffer, va_arg(args, unsigned int), 16, ch == 'X');
            }
            break;
        case 'p':
        {
            unsigned long value = (unsigned long)va_arg(args, void *);
            buffer[0] = '0';
            buffer[1] = 'x';
            text_length = 2 + format_unsigned(buffer + 2, value, 16, 0);
            break;
        }
        case 'c':
            buffer[0] = (char)va_arg(args, int);
            buffer[1] = '\0';
            text_length = 1;
            break;
        case 's':
            text = va_arg(args, const char *);
            if (!text)
            {
                text = "(null)";
            }
            text_length = (int)app_strlen_internal(text);
            break;
        case '%':
            buffer[0] = '%';
            buffer[1] = '\0';
            text_length = 1;
            break;
        default:
            buffer[0] = '%';
            buffer[1] = ch;
            buffer[2] = '\0';
            text_length = 2;
            break;
        }

        if (width > text_length)
        {
            write_padding(emit, ctx, width - text_length, zero_pad ? '0' : ' ');
            total += width - text_length;
        }

        for (int index = 0; index < text_length; index++)
        {
            emit(ctx, text[index]);
        }
        total += text_length;
    }

    return total;
}

int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args)
{
    BufferFormatState state = {buffer, size, 0};
    va_list args_copy;
    int written;

    va_copy(args_copy, args);
    written = app_vformat(buffer_emit, &state, fmt, args_copy);
    va_end(args_copy);
    terminate_buffer(&state);
    return written;
}

int snprintf(char *buffer, size_t size, const char *fmt, ...)
{
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return written;
}

int printf(const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    user_kernel_write(buffer);
    return written;
}

int puts(const char *text)
{
    if (!text)
    {
        text = "(null)";
    }

    user_kernel_write(text);
    user_kernel_write("\n");
    return 0;
}

void fprint(char *buffer, const char *fmt, ...)
{
    RawFormatState state = {buffer, 0};
    va_list args;

    va_start(args, fmt);
    app_vformat(raw_emit, &state, fmt, args);
    va_end(args);
    terminate_raw(&state);
}