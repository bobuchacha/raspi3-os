#define CRT_EXPORTS 1
#define CRT_INTERNAL_BUILD 1

#include "crt.h"
#include "user_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define CRT_MAX_ATEXIT 32U
#define CRT_STDIO_BUFFER_SIZE 512U
#define CRT_FILE_MAGIC 0x43525446UL
#define CRT_SIGNAL_COUNT 32U
#define CRT_LOCALE_NAME_MAX 32U
#define CRT_TIME_DAY_SECONDS 86400UL
#define CRT_TIME_HOUR_SECONDS 3600UL
#define CRT_TIME_MINUTE_SECONDS 60UL

#ifndef CRT_PI
#define CRT_PI 3.14159265358979323846
#endif

#ifndef CRT_EPSILON
#define CRT_EPSILON 1.0e-12
#endif

typedef struct FormatSink {
    char* buffer;
    size_t capacity;
    size_t length;
} FormatSink;

typedef struct CrtAlignedAllocationHeader {
    uintptr_t magic;
    void* raw;
} CrtAlignedAllocationHeader;

static const uintptr_t CRT_ALIGNED_ALLOCATION_MAGIC = 0x435254414C49474EULL;

void* memcpy(void* dest, const void* src, size_t size);
void* memmove(void* dest, const void* src, size_t size);
void* memset(void* dest, int value, size_t size);
int memcmp(const void* lhs, const void* rhs, size_t size);
size_t strlen(const char* text);
size_t strnlen(const char* text, size_t max_size);
int strcmp(const char* lhs, const char* rhs);
int strncmp(const char* lhs, const char* rhs, size_t size);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t size);
char* strchr(const char* text, int value);
char* strstr(const char* haystack, const char* needle);
size_t fread(void* buffer, size_t size, size_t count, FILE* stream);
size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream);

static void format_push_char(FormatSink* sink, char ch) {
    if (sink == NULL) {
        return;
    }

    if ((sink->buffer != NULL) && (sink->capacity > 0U) && (sink->length + 1U < sink->capacity)) {
        sink->buffer[sink->length] = ch;
    }
    sink->length++;
}

static void format_push_text(FormatSink* sink, const char* text) {
    if (text == NULL) {
        text = "(null)";
    }

    while (*text != '\0') {
        format_push_char(sink, *text++);
    }
}

static void format_push_padding(FormatSink* sink, int count, char pad) {
    while (count-- > 0) {
        format_push_char(sink, pad);
    }
}

static size_t format_text_length(const char* text) {
    size_t length = 0U;

    if (text == NULL) {
        return 6U;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static size_t format_unsigned(char* buffer, unsigned long long value, unsigned int base, int uppercase) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char* digits = uppercase ? digits_upper : digits_lower;
    char scratch[32];
    size_t length = 0U;

    if (base < 2U || base > 16U) {
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

    for (size_t index = 0U; index < length; ++index) {
        buffer[index] = scratch[length - 1U - index];
    }

    return length;
}

static void format_number(
    FormatSink* sink,
    unsigned long long value,
    int negative,
    unsigned int base,
    int uppercase,
    int width,
    int zero_pad,
    int prefix_hex
) {
    char digits[32];
    size_t length = format_unsigned(digits, value, base, uppercase);
    int prefix_length = 0;

    if (negative) {
        ++prefix_length;
    }
    if (prefix_hex) {
        prefix_length += 2;
    }

    if (!zero_pad) {
        format_push_padding(sink, width - (int)(length + (size_t)prefix_length), ' ');
    }
    if (negative) {
        format_push_char(sink, '-');
    }
    if (prefix_hex) {
        format_push_char(sink, '0');
        format_push_char(sink, uppercase ? 'X' : 'x');
    }
    if (zero_pad) {
        format_push_padding(sink, width - (int)(length + (size_t)prefix_length), '0');
    }

    for (size_t index = 0U; index < length; ++index) {
        format_push_char(sink, digits[index]);
    }
}

static int format_vprintf(FormatSink* sink, const char* fmt, va_list args) {
    while ((fmt != NULL) && (*fmt != '\0')) {
        int zero_pad = 0;
        int width = 0;
        int long_count = 0;
        char spec;

        if (*fmt != '%') {
            format_push_char(sink, *fmt++);
            continue;
        }

        ++fmt;
        if (*fmt == '%') {
            format_push_char(sink, *fmt++);
            continue;
        }
        if (*fmt == '0') {
            zero_pad = 1;
            ++fmt;
        }
        while ((*fmt >= '0') && (*fmt <= '9')) {
            width = (width * 10) + (*fmt - '0');
            ++fmt;
        }
        while ((*fmt == 'l') || (*fmt == 'z')) {
            if (*fmt == 'l') {
                ++long_count;
            }
            else {
                long_count = 1;
            }
            ++fmt;
        }

        spec = *fmt ? *fmt++ : '\0';
        switch (spec) {
        case '\0':
            break;
        case 'c':
            format_push_char(sink, (char)va_arg(args, int));
            break;
        case 's': {
            const char* text = va_arg(args, const char*);
            size_t length = format_text_length(text);

            format_push_padding(sink, width - (int)length, ' ');
            format_push_text(sink, text);
            break;
        }
        case 'd':
        case 'i': {
            long long value;
            unsigned long long magnitude;

            if (long_count > 1) {
                value = va_arg(args, long long);
            }
            else if (long_count == 1) {
                value = va_arg(args, long);
            }
            else {
                value = va_arg(args, int);
            }
            magnitude = value < 0 ? (unsigned long long)(-value) : (unsigned long long)value;
            format_number(sink, magnitude, value < 0, 10U, 0, width, zero_pad, 0);
            break;
        }
        case 'u': {
            unsigned long long value;

            if (long_count > 1) {
                value = va_arg(args, unsigned long long);
            }
            else if (long_count == 1) {
                value = va_arg(args, unsigned long);
            }
            else {
                value = va_arg(args, unsigned int);
            }
            format_number(sink, value, 0, 10U, 0, width, zero_pad, 0);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long long value;

            if (long_count > 1) {
                value = va_arg(args, unsigned long long);
            }
            else if (long_count == 1) {
                value = va_arg(args, unsigned long);
            }
            else {
                value = va_arg(args, unsigned int);
            }
            format_number(sink, value, 0, 16U, spec == 'X', width, zero_pad, 0);
            break;
        }
        case 'p': {
            unsigned long long value = (unsigned long long)(uintptr_t)va_arg(args, void*);
            format_number(sink, value, 0, 16U, 0, width > 0 ? width : 2, 1, 1);
            break;
        }
        default:
            format_push_char(sink, '%');
            format_push_char(sink, spec);
            break;
        }
    }

    if ((sink != NULL) && (sink->buffer != NULL) && (sink->capacity > 0U)) {
        size_t terminator = sink->length < sink->capacity ? sink->length : sink->capacity - 1U;
        sink->buffer[terminator] = '\0';
    }

    return sink != NULL ? (int)sink->length : 0;
}

typedef struct CrtExitHandlerNode {
    crt_atexit_fn handler;
    struct CrtExitHandlerNode* next;
} CrtExitHandlerNode;

typedef struct CrtEnvironmentEntry {
    char* name;
    char* value;
    struct CrtEnvironmentEntry* next;
} CrtEnvironmentEntry;

typedef struct CrtFile {
    unsigned long magic;
    unsigned long flags;
    char* path;
    char* mode;
    unsigned long position;
    unsigned long size;
    unsigned long capacity;
    char* buffer;
    int error;
    int eof;
} CrtFile;

typedef struct CrtSignalEntry {
    crt_signal_handler_fn handler;
} CrtSignalEntry;

typedef struct CrtLocaleState {
    char name[CRT_LOCALE_NAME_MAX];
    struct lconv locale;
    char decimal_point[2];
    char thousands_sep[2];
    char grouping[2];
    char int_curr_symbol[5];
    char currency_symbol[2];
    char mon_decimal_point[2];
    char mon_thousands_sep[2];
    char mon_grouping[2];
    char positive_sign[2];
    char negative_sign[2];
} CrtLocaleState;

static CrtExitHandlerNode* g_atexit_handlers;
static CrtEnvironmentEntry* g_environment_head;
static CrtSignalEntry g_signal_handlers[CRT_SIGNAL_COUNT];
static CrtLocaleState g_locale_state;
static struct lconv g_locale_view;
static FILE* g_standard_input;
static FILE* g_standard_output;
static FILE* g_standard_error;
static uintptr_t g_stack_chk_guard = 0x6d5a3c1f4b92a871ULL;

static void crt_initialize_locale(void);
static int crt_ascii_tolower(int value);
static int crt_ascii_toupper(int value);
static int crt_ascii_isalpha(int value);
static int crt_ascii_isdigit(int value);
static int crt_ascii_isspace(int value);
static size_t crt_string_length(const char* text);
static void* crt_memory_move(void* destination, const void* source, size_t size);
static long crt_parse_unsigned(const char* text, char** endptr, int base, int negative, unsigned long* value_out);
static double crt_pow_int(double base, long exponent);
static double crt_parse_decimal_fraction(const char* text, const char** endptr, long* exponent_adjustment);
static long crt_write_file_text(FILE* stream, const char* text);
static int crt_compare_bytes(const unsigned char* lhs, const unsigned char* rhs, size_t size);
static FILE* crt_allocate_stream(void);
static void crt_free_stream(FILE* stream);
static CrtFile* crt_stream_from_FILE(FILE* stream);
static FILE* crt_file_from_path_mode(const char* path, const char* mode);
static size_t crt_file_read(void* buffer, size_t size, size_t count, FILE* stream);
static size_t crt_file_write(const void* buffer, size_t size, size_t count, FILE* stream);
static int crt_file_seek(FILE* stream, long offset, int origin);
static long crt_file_tell(FILE* stream);
static int crt_file_flush(FILE* stream);
static char* crt_copy_string(char* destination, const char* source);
static char* crt_copy_string_limited(char* destination, const char* source, size_t size);
static char* crt_duplicate_string(const char* source);
static CrtEnvironmentEntry* crt_find_env_entry(const char* name);
static void crt_free_env_entry(CrtEnvironmentEntry* entry);
static int crt_store_env_pair(const char* name, const char* value);
static const char* crt_find_env_value(const char* name);
static void crt_remove_env_pair(const char* name);
static void crt_clear_env_pairs(void);
static void crt_clear_error_flags(FILE* stream);
static int crt_signal_trampoline(int signum);
static void crt_time_to_tm(long seconds, tm* out);
static long crt_tm_to_time(const tm* value);

struct FILE {
    unsigned long magic;
    unsigned long flags;
    unsigned char* buffer;
    size_t position;
    size_t size;
    size_t capacity;
    int error;
    int eof;
    char mode[8];
    char path[128];
};

static void crt_initialize_locale(void) {
    g_locale_state.locale.decimal_point = g_locale_state.decimal_point;
    g_locale_state.locale.thousands_sep = g_locale_state.thousands_sep;
    g_locale_state.locale.grouping = g_locale_state.grouping;
    g_locale_state.locale.int_curr_symbol = g_locale_state.int_curr_symbol;
    g_locale_state.locale.currency_symbol = g_locale_state.currency_symbol;
    g_locale_state.locale.mon_decimal_point = g_locale_state.mon_decimal_point;
    g_locale_state.locale.mon_thousands_sep = g_locale_state.mon_thousands_sep;
    g_locale_state.locale.mon_grouping = g_locale_state.mon_grouping;
    g_locale_state.locale.positive_sign = g_locale_state.positive_sign;
    g_locale_state.locale.negative_sign = g_locale_state.negative_sign;
    g_locale_state.locale.int_frac_digits = 2;
    g_locale_state.locale.frac_digits = 2;
    g_locale_state.locale.p_cs_precedes = 1;
    g_locale_state.locale.p_sep_by_space = 0;
    g_locale_state.locale.n_cs_precedes = 1;
    g_locale_state.locale.n_sep_by_space = 0;
    g_locale_state.locale.p_sign_posn = 1;
    g_locale_state.locale.n_sign_posn = 1;
    g_locale_state.locale.int_p_cs_precedes = 1;
    g_locale_state.locale.int_p_sep_by_space = 0;
    g_locale_state.locale.int_n_cs_precedes = 1;
    g_locale_state.locale.int_n_sep_by_space = 0;
    g_locale_state.locale.int_p_sign_posn = 1;
    g_locale_state.locale.int_n_sign_posn = 1;
    crt_copy_string(g_locale_state.name, "C");
    crt_copy_string(g_locale_state.decimal_point, ".");
    crt_copy_string(g_locale_state.thousands_sep, "");
    crt_copy_string(g_locale_state.grouping, "");
    crt_copy_string(g_locale_state.int_curr_symbol, "USD ");
    crt_copy_string(g_locale_state.currency_symbol, "$ ");
    crt_copy_string(g_locale_state.mon_decimal_point, ".");
    crt_copy_string(g_locale_state.mon_thousands_sep, "");
    crt_copy_string(g_locale_state.mon_grouping, "");
    crt_copy_string(g_locale_state.positive_sign, "");
    crt_copy_string(g_locale_state.negative_sign, "-");
    g_locale_view = g_locale_state.locale;
}

static int crt_ascii_tolower(int value) {
    if (value >= 'A' && value <= 'Z') {
        return value + ('a' - 'A');
    }
    return value;
}

static int crt_ascii_toupper(int value) {
    if (value >= 'a' && value <= 'z') {
        return value - ('a' - 'A');
    }
    return value;
}

static int crt_ascii_isalpha(int value) {
    return ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'));
}

static int crt_ascii_isdigit(int value) {
    return (value >= '0' && value <= '9');
}

static int crt_ascii_isspace(int value) {
    return (value == ' ') || (value == '\t') || (value == '\n') || (value == '\r') || (value == '\f') || (value == '\v');
}

static size_t crt_string_length(const char* text) {
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

static void* crt_memory_move(void* destination, const void* source, size_t size) {
    unsigned char* dest_bytes = (unsigned char*)destination;
    const unsigned char* src_bytes = (const unsigned char*)source;

    if (dest_bytes == src_bytes || size == 0U) {
        return destination;
    }

    if (dest_bytes < src_bytes || dest_bytes >= (src_bytes + size)) {
        for (size_t index = 0U; index < size; ++index) {
            dest_bytes[index] = src_bytes[index];
        }
        return destination;
    }

    while (size > 0U) {
        --size;
        dest_bytes[size] = src_bytes[size];
    }

    return destination;
}

static long crt_parse_unsigned(const char* text, char** endptr, int base, int negative, unsigned long* value_out) {
    unsigned long value = 0UL;
    int used_base = base;
    const char* cursor = text;
    int digit;

    if ((text == NULL) || (value_out == NULL)) {
        return StatusInvalidArgument;
    }

    while (crt_ascii_isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        ++cursor;
    }

    if (used_base == 0) {
        used_base = 10;
        if (cursor[0] == '0') {
            if ((cursor[1] == 'x' || cursor[1] == 'X') && ((base == 0) || (base == 16))) {
                used_base = 16;
                cursor += 2;
            }
            else {
                used_base = 8;
            }
        }
    }
    else if ((used_base == 16) && (cursor[0] == '0') && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }

    while (*cursor != '\0') {
        if (crt_ascii_isdigit((unsigned char)*cursor)) {
            digit = *cursor - '0';
        }
        else if (crt_ascii_isalpha((unsigned char)*cursor)) {
            digit = crt_ascii_tolower(*cursor) - 'a' + 10;
        }
        else {
            break;
        }

        if (digit < 0 || digit >= used_base) {
            break;
        }

        value = (value * (unsigned long)used_base) + (unsigned long)digit;
        ++cursor;
    }

    if (endptr != NULL) {
        *endptr = (char*)cursor;
    }

    if (negative) {
        *value_out = (unsigned long)(-(long)value);
    }
    else {
        *value_out = value;
    }

    return StatusOK;
}

static double crt_pow_int(double base, long exponent) {
    double result = 1.0;
    long power = exponent;

    if (power < 0) {
        base = 1.0 / base;
        power = -power;
    }

    while (power > 0) {
        if ((power & 1L) != 0L) {
            result *= base;
        }
        base *= base;
        power >>= 1;
    }

    return result;
}

static double crt_parse_decimal_fraction(const char* text, const char** endptr, long* exponent_adjustment) {
    double value = 0.0;
    double scale = 1.0;
    const char* cursor = text;

    while (crt_ascii_isdigit((unsigned char)*cursor)) {
        value = (value * 10.0) + (double)(*cursor - '0');
        scale *= 10.0;
        ++cursor;
    }

    if (endptr != NULL) {
        *endptr = cursor;
    }
    if (exponent_adjustment != NULL) {
        *exponent_adjustment = 0L;
    }

    return (scale != 0.0) ? (value / scale) : value;
}

static FILE* crt_allocate_stream(void) {
    return (FILE*)malloc(sizeof(FILE));
}

static void crt_free_stream(FILE* stream) {
    if (stream != NULL) {
        free(stream);
    }
}

static CrtFile* crt_stream_from_FILE(FILE* stream) {
    return (CrtFile*)stream;
}

static char* crt_copy_string(char* destination, const char* source) {
    size_t index = 0U;

    if (destination == NULL) {
        return destination;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return destination;
    }

    while (source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    return destination;
}

static char* crt_copy_string_limited(char* destination, const char* source, size_t size) {
    size_t index = 0U;

    if (destination == NULL || size == 0U) {
        return destination;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return destination;
    }

    while ((index + 1U) < size && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    return destination;
}

/*
 * Duplicate one NUL-terminated string onto the CRT heap.
 *
 * Environment variables must retain stable storage after callers reuse or drop
 * their input buffers, so the CRT keeps private copies instead of borrowing
 * caller-owned memory.
 *
 * @param source Source text to duplicate.
 * @return Heap-owned copy, or NULL on allocation failure.
 */
static char* crt_duplicate_string(const char* source) {
    char* copy;
    size_t length;

    if (source == NULL) {
        return NULL;
    }

    length = crt_string_length(source);
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }

    crt_copy_string(copy, source);
    return copy;
}

/*
 * Find one environment entry by exact name.
 *
 * @param name Variable name to resolve.
 * @return Matching entry, or NULL when the variable is unset.
 */
static CrtEnvironmentEntry* crt_find_env_entry(const char* name) {
    CrtEnvironmentEntry* entry;

    if (name == NULL) {
        return NULL;
    }

    for (entry = g_environment_head; entry != NULL; entry = entry->next) {
        if ((entry->name != NULL) && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }

    return NULL;
}

/*
 * Release one environment entry and its copied name/value strings.
 *
 * @param entry Entry to destroy.
 * @return Nothing.
 */
static void crt_free_env_entry(CrtEnvironmentEntry* entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->name);
    free(entry->value);
    entry->name = NULL;
    entry->value = NULL;
    entry->next = NULL;
    free(entry);
}

/*
 * Store or replace one environment pair in the process-local CRT registry.
 *
 * The registry grows dynamically so shell-style workloads do not fail after a
 * small fixed number of `setenv` calls.
 *
 * @param name Variable name to create or update.
 * @param value Variable value to copy into the registry.
 * @return Zero on success, or -1 on allocation failure.
 */
static int crt_store_env_pair(const char* name, const char* value) {
    CrtEnvironmentEntry* entry;
    char* name_copy;
    char* value_copy;

    entry = crt_find_env_entry(name);
    if (entry != NULL) {
        value_copy = crt_duplicate_string(value);
        if (value_copy == NULL) {
            return -1;
        }

        free(entry->value);
        entry->value = value_copy;
        return 0;
    }

    name_copy = crt_duplicate_string(name);
    if (name_copy == NULL) {
        return -1;
    }

    value_copy = crt_duplicate_string(value);
    if (value_copy == NULL) {
        free(name_copy);
        return -1;
    }

    entry = (CrtEnvironmentEntry*)malloc(sizeof(*entry));
    if (entry == NULL) {
        free(name_copy);
        free(value_copy);
        return -1;
    }

    entry->name = name_copy;
    entry->value = value_copy;
    entry->next = g_environment_head;
    g_environment_head = entry;
    return 0;
}

/*
 * Look up one environment value string by exact variable name.
 *
 * @param name Variable name to resolve.
 * @return Stored value string, or NULL when the variable is unset.
 */
static const char* crt_find_env_value(const char* name) {
    CrtEnvironmentEntry* entry = crt_find_env_entry(name);

    return entry != NULL ? entry->value : NULL;
}

/*
 * Remove one environment pair from the process-local CRT registry.
 *
 * @param name Variable name to remove.
 * @return Nothing.
 */
static void crt_remove_env_pair(const char* name) {
    CrtEnvironmentEntry* entry = g_environment_head;
    CrtEnvironmentEntry* previous = NULL;

    while (entry != NULL) {
        if ((entry->name != NULL) && strcmp(entry->name, name) == 0) {
            if (previous != NULL) {
                previous->next = entry->next;
            }
            else {
                g_environment_head = entry->next;
            }
            crt_free_env_entry(entry);
            return;
        }

        previous = entry;
        entry = entry->next;
    }
}

/*
 * Remove every environment pair from the process-local CRT registry.
 *
 * @return Nothing.
 */
static void crt_clear_env_pairs(void) {
    CrtEnvironmentEntry* entry = g_environment_head;
    CrtEnvironmentEntry* next;

    while (entry != NULL) {
        next = entry->next;
        crt_free_env_entry(entry);
        entry = next;
    }

    g_environment_head = NULL;
}

static void crt_clear_error_flags(FILE* stream) {
    CrtFile* file = crt_stream_from_FILE(stream);

    if (file != NULL) {
        file->error = 0;
        file->eof = 0;
    }
}

static void crt_time_to_tm(long seconds, tm* out) {
    long days;
    long remainder;
    int year;
    static const int month_lengths[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

    if (out == NULL) {
        return;
    }

    days = seconds / (long)CRT_TIME_DAY_SECONDS;
    remainder = seconds % (long)CRT_TIME_DAY_SECONDS;
    if (remainder < 0) {
        remainder += CRT_TIME_DAY_SECONDS;
        --days;
    }

    out->tm_hour = (int)(remainder / (long)CRT_TIME_HOUR_SECONDS);
    remainder %= (long)CRT_TIME_HOUR_SECONDS;
    out->tm_min = (int)(remainder / (long)CRT_TIME_MINUTE_SECONDS);
    out->tm_sec = (int)(remainder % (long)CRT_TIME_MINUTE_SECONDS);
    out->tm_wday = (int)((days + 4L) % 7L);
    if (out->tm_wday < 0) {
        out->tm_wday += 7;
    }

    year = 1970;
    while (days >= 365L) {
        long year_length = 365L;
        if (((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0)) {
            year_length = 366L;
        }
        if (days < year_length) {
            break;
        }
        days -= year_length;
        ++year;
    }

    out->tm_year = year - 1900;
    out->tm_yday = (int)days;
    out->tm_mon = 0;
    for (int month = 0; month < 12; ++month) {
        int month_length = month_lengths[month];
        if (month == 1 && (((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0))) {
            month_length = 29;
        }
        if (days < month_length) {
            out->tm_mon = month;
            out->tm_mday = (int)days + 1;
            return;
        }
        days -= month_length;
    }

    out->tm_mday = 1;
}

static long crt_tm_to_time(const tm* value) {
    long days = 0L;
    int year;
    static const int month_lengths[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

    if (value == NULL) {
        return 0L;
    }

    for (year = 1970; year < (value->tm_year + 1900); ++year) {
        days += 365L;
        if (((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0)) {
            ++days;
        }
    }

    for (int month = 0; month < value->tm_mon; ++month) {
        int month_length = month_lengths[month];
        if (month == 1 && (((value->tm_year + 1900) % 4) == 0 && ((value->tm_year + 1900) % 100) != 0) || (((value->tm_year + 1900) % 400) == 0)) {
            month_length = 29;
        }
        days += month_length;
    }

    days += (long)value->tm_mday - 1L;
    return (days * (long)CRT_TIME_DAY_SECONDS)
        + ((long)value->tm_hour * (long)CRT_TIME_HOUR_SECONDS)
        + ((long)value->tm_min * (long)CRT_TIME_MINUTE_SECONDS)
        + (long)value->tm_sec;
}

static FILE* crt_file_from_path_mode(const char* path, const char* mode) {
    FILE* stream = crt_allocate_stream();
    CrtFile* file;
    long read_status;
    char* payload;
    size_t file_size = 0U;

    if (stream == NULL) {
        return NULL;
    }

    file = crt_stream_from_FILE(stream);
    memset(file, 0, sizeof(*file));
    file->magic = CRT_FILE_MAGIC;
    crt_copy_string(file->path, path ? path : "");
    crt_copy_string(file->mode, mode ? mode : "r");

    if (mode != NULL && (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL)) {
        file->flags |= 1UL;
    }

    if (path != NULL && (strchr(file->mode, 'r') != NULL || strchr(file->mode, '+') != NULL)) {
        unsigned long offset = 0UL;
        char buffer[CRT_STDIO_BUFFER_SIZE];

        file->buffer = (char*)malloc(1U);
        if (file->buffer == NULL) {
            crt_free_stream(stream);
            return NULL;
        }

        for (;;) {
            read_status = readFile(path, offset, buffer, sizeof(buffer));
            if (read_status < 0) {
                free(file->buffer);
                crt_free_stream(stream);
                return NULL;
            }
            if (read_status == 0) {
                break;
            }

            payload = (char*)realloc(file->buffer, file_size + (size_t)read_status + 1U);
            if (payload == NULL) {
                free(file->buffer);
                crt_free_stream(stream);
                return NULL;
            }
            file->buffer = payload;
            memcpy(file->buffer + file_size, buffer, (size_t)read_status);
            file_size += (size_t)read_status;
            offset += (unsigned long)read_status;
            if ((size_t)read_status < sizeof(buffer)) {
                break;
            }
        }

        file->buffer[file_size] = '\0';
        file->size = file_size;
        file->capacity = file_size + 1U;
    }

    return stream;
}

static size_t crt_file_read(void* buffer, size_t size, size_t count, FILE* stream) {
    CrtFile* file = crt_stream_from_FILE(stream);
    size_t requested = size * count;
    size_t remaining;

    if (file == NULL || buffer == NULL || size == 0U || count == 0U || file->buffer == NULL) {
        return 0U;
    }

    if (file->position >= file->size) {
        file->eof = 1;
        return 0U;
    }

    remaining = file->size - file->position;
    if (requested > remaining) {
        requested = remaining;
        file->eof = 1;
    }

    memcpy(buffer, file->buffer + file->position, requested);
    file->position += requested;
    return requested / size;
}

static size_t crt_file_write(const void* buffer, size_t size, size_t count, FILE* stream) {
    CrtFile* file = crt_stream_from_FILE(stream);
    size_t requested = size * count;

    if (file == NULL || buffer == NULL || size == 0U || count == 0U) {
        return 0U;
    }

    if (file->buffer != NULL && file->position + requested + 1U > file->capacity) {
        char* payload = (char*)realloc(file->buffer, file->position + requested + 1U);
        if (payload == NULL) {
            file->error = 1;
            return 0U;
        }
        file->buffer = payload;
        file->capacity = file->position + requested + 1U;
    }
    else if (file->buffer == NULL) {
        file->buffer = (char*)malloc(requested + 1U);
        if (file->buffer == NULL) {
            file->error = 1;
            return 0U;
        }
        file->capacity = requested + 1U;
        file->size = 0U;
        file->position = 0U;
    }

    memcpy(file->buffer + file->position, buffer, requested);
    file->position += requested;
    if (file->position > file->size) {
        file->size = file->position;
    }
    file->buffer[file->size] = '\0';
    return count;
}

static int crt_file_seek(FILE* stream, long offset, int origin) {
    CrtFile* file = crt_stream_from_FILE(stream);
    long new_position;

    if (file == NULL) {
        return -1;
    }

    switch (origin) {
    case 0:
        new_position = offset;
        break;
    case 1:
        new_position = (long)file->position + offset;
        break;
    case 2:
        new_position = (long)file->size + offset;
        break;
    default:
        return -1;
    }

    if (new_position < 0L) {
        return -1;
    }

    file->position = (size_t)new_position;
    file->eof = 0;
    return 0;
}

static long crt_file_tell(FILE* stream) {
    CrtFile* file = crt_stream_from_FILE(stream);

    if (file == NULL) {
        return -1L;
    }

    return (long)file->position;
}

static int crt_file_flush(FILE* stream) {
    (void)stream;
    return 0;
}

static long crt_write_file_text(FILE* stream, const char* text) {
    const char* payload = text != NULL ? text : "(null)";
    size_t length = strlen(payload);

    if (stream == NULL) {
        return writeText(payload);
    }

    if (fwrite(payload, 1U, length, stream) != length) {
        return -1L;
    }

    return 0L;
}

static int crt_compare_bytes(const unsigned char* lhs, const unsigned char* rhs, size_t size) {
    for (size_t index = 0U; index < size; ++index) {
        if (lhs[index] != rhs[index]) {
            return (lhs[index] < rhs[index]) ? -1 : 1;
        }
    }

    return 0;
}

static int crt_signal_trampoline(int signum) {
    if ((signum >= 0) && ((unsigned long)signum < CRT_SIGNAL_COUNT) && g_signal_handlers[signum].handler != NULL) {
        g_signal_handlers[signum].handler(signum);
    }
    return 0;
}

int crt_main(crt_main_fn main_fn, int argc, char** argv) {
    if (main_fn == NULL) {
        return -1;
    }

    return main_fn(argc, argv);
}

int atexit(void (*function)(void)) {
    CrtExitHandlerNode* node;

    if (function == NULL) {
        return -1;
    }

    node = (CrtExitHandlerNode*)malloc(sizeof(CrtExitHandlerNode));
    if (node == NULL) {
        return -1;
    }

    node->handler = function;
    node->next = g_atexit_handlers;
    g_atexit_handlers = node;
    return 0;
}

void exit(int code) {
    CrtExitHandlerNode* node = g_atexit_handlers;

    while (node != NULL) {
        node->handler();
        node = node->next;
    }

    exitProcess((unsigned long)(unsigned int)code);
}

void abort(void) {
    if (writeLine("crt abort") < 0) {
        ;
    }
    exitProcess(255U);
}

char* getenv(const char* name) {
    return (char*)crt_find_env_value(name);
}

int setenv(const char* name, const char* value, int overwrite) {
    if (name == NULL || name[0] == '\0' || value == NULL) {
        return -1;
    }
    if (!overwrite && crt_find_env_value(name) != NULL) {
        return 0;
    }

    return crt_store_env_pair(name, value);
}

int unsetenv(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    crt_remove_env_pair(name);
    return 0;
}

int clearenv(void) {
    crt_clear_env_pairs();
    return 0;
}

void* malloc(size_t size) {
    return user_shared_heap_malloc(size);
}

void free(void* ptr) {
    CrtAlignedAllocationHeader* header;

    if (ptr == NULL) {
        return;
    }

    header = (CrtAlignedAllocationHeader*)((unsigned char*)ptr - sizeof(CrtAlignedAllocationHeader));
    if ((header->magic == CRT_ALIGNED_ALLOCATION_MAGIC)
        && (header->raw != NULL)
        && user_shared_heap_contains_pointer(header->raw)) {
        user_shared_heap_free(header->raw);
        return;
    }

    user_shared_heap_free(ptr);
}

void* calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* memory = malloc(total ? total : 1U);

    if (memory != NULL) {
        memset(memory, 0, total);
    }
    return memory;
}

void* realloc(void* ptr, size_t size) {
    return user_shared_heap_realloc(ptr, size);
}

void* aligned_alloc(size_t alignment, size_t size) {
    size_t total;
    unsigned char* raw;
    uintptr_t aligned;
    CrtAlignedAllocationHeader* header;

    if (alignment == 0U) {
        return NULL;
    }
    if ((alignment & (alignment - 1U)) != 0U) {
        return NULL;
    }
    if (alignment > (SIZE_MAX - sizeof(CrtAlignedAllocationHeader))) {
        return NULL;
    }
    if (size > (SIZE_MAX - alignment - sizeof(CrtAlignedAllocationHeader))) {
        return NULL;
    }

    total = size + alignment + sizeof(CrtAlignedAllocationHeader);
    raw = (unsigned char*)malloc(total);
    if (raw == NULL) {
        return NULL;
    }

    aligned = ((uintptr_t)(raw + sizeof(CrtAlignedAllocationHeader)) + (alignment - 1U)) & ~(alignment - 1U);
    header = (CrtAlignedAllocationHeader*)(aligned - sizeof(CrtAlignedAllocationHeader));
    header->magic = CRT_ALIGNED_ALLOCATION_MAGIC;
    header->raw = raw;
    return (void*)aligned;
}

void* memcpy(void* dest, const void* src, size_t size) {
    unsigned char* destination = (unsigned char*)dest;
    const unsigned char* source = (const unsigned char*)src;

    for (size_t index = 0U; index < size; ++index) {
        destination[index] = source[index];
    }

    return dest;
}

void* memmove(void* dest, const void* src, size_t size) {
    return crt_memory_move(dest, src, size);
}

void* memset(void* dest, int value, size_t size) {
    unsigned char* bytes = (unsigned char*)dest;

    for (size_t index = 0U; index < size; ++index) {
        bytes[index] = (unsigned char)value;
    }

    return dest;
}

int memcmp(const void* lhs, const void* rhs, size_t size) {
    return crt_compare_bytes((const unsigned char*)lhs, (const unsigned char*)rhs, size);
}

size_t strlen(const char* text) {
    return crt_string_length(text);
}

size_t strnlen(const char* text, size_t max_size) {
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }

    while ((length < max_size) && (text[length] != '\0')) {
        ++length;
    }

    return length;
}

int strcmp(const char* lhs, const char* rhs) {
    size_t index = 0U;

    if (lhs == rhs) {
        return 0;
    }
    if (lhs == NULL) {
        return -1;
    }
    if (rhs == NULL) {
        return 1;
    }

    while (lhs[index] != '\0' && rhs[index] != '\0') {
        if (lhs[index] != rhs[index]) {
            return (unsigned char)lhs[index] < (unsigned char)rhs[index] ? -1 : 1;
        }
        ++index;
    }

    if (lhs[index] == rhs[index]) {
        return 0;
    }
    return lhs[index] == '\0' ? -1 : 1;
}

int strncmp(const char* lhs, const char* rhs, size_t size) {
    size_t index = 0U;

    if (lhs == rhs) {
        return 0;
    }
    if (lhs == NULL) {
        return -1;
    }
    if (rhs == NULL) {
        return 1;
    }

    while (index < size && lhs[index] != '\0' && rhs[index] != '\0') {
        if (lhs[index] != rhs[index]) {
            return (unsigned char)lhs[index] < (unsigned char)rhs[index] ? -1 : 1;
        }
        ++index;
    }

    if (index == size) {
        return 0;
    }
    if (lhs[index] == rhs[index]) {
        return 0;
    }
    return lhs[index] == '\0' ? -1 : 1;
}

char* strcpy(char* dest, const char* src) {
    size_t index = 0U;

    if (dest == NULL || src == NULL) {
        return dest;
    }

    while ((dest[index] = src[index]) != '\0') {
        ++index;
    }

    return dest;
}

char* strncpy(char* dest, const char* src, size_t size) {
    size_t index = 0U;

    if (dest == NULL || size == 0U) {
        return dest;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return dest;
    }

    while (index < size && src[index] != '\0') {
        dest[index] = src[index];
        ++index;
    }
    while (index < size) {
        dest[index++] = '\0';
    }

    return dest;
}

char* strchr(const char* text, int value) {
    if (text == NULL) {
        return NULL;
    }

    while (*text != '\0') {
        if (*text == (char)value) {
            return (char*)text;
        }
        ++text;
    }

    return value == '\0' ? (char*)text : NULL;
}

char* strstr(const char* haystack, const char* needle) {
    size_t needle_length;

    if (haystack == NULL || needle == NULL) {
        return NULL;
    }
    if (needle[0] == '\0') {
        return (char*)haystack;
    }

    needle_length = strlen(needle);
    for (; *haystack != '\0'; ++haystack) {
        if ((haystack[0] == needle[0]) && (strncmp(haystack, needle, needle_length) == 0)) {
            return (char*)haystack;
        }
    }

    return NULL;
}

int isalpha(int value) { return crt_ascii_isalpha(value); }
int isdigit(int value) { return crt_ascii_isdigit(value); }
int isspace(int value) { return crt_ascii_isspace(value); }
int tolower(int value) { return crt_ascii_tolower(value); }
int toupper(int value) { return crt_ascii_toupper(value); }

char* setlocale(int category, const char* locale_name) {
    (void)category;
    if (locale_name != NULL && locale_name[0] != '\0') {
        crt_copy_string(g_locale_state.name, locale_name);
    }
    return g_locale_state.name;
}

struct lconv* localeconv(void) {
    return &g_locale_view;
}

long strtol(const char* text, char** endptr, int base) {
    unsigned long value = 0UL;
    long status;

    status = crt_parse_unsigned(text, endptr, base, 0, &value);
    if (status != StatusOK) {
        return 0L;
    }
    return (long)value;
}

unsigned long strtoul(const char* text, char** endptr, int base) {
    unsigned long value = 0UL;
    long status;

    status = crt_parse_unsigned(text, endptr, base, 0, &value);
    if (status != StatusOK) {
        return 0UL;
    }
    return value;
}

double strtod(const char* text, char** endptr) {
    const char* cursor = text;
    int negative = 0;
    double whole = 0.0;
    double fraction = 0.0;
    double result;
    long exponent = 0L;
    long exponent_sign = 1L;

    if (cursor == NULL) {
        if (endptr != NULL) {
            *endptr = NULL;
        }
        return 0.0;
    }

    while (crt_ascii_isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        ++cursor;
    }
    while (crt_ascii_isdigit((unsigned char)*cursor)) {
        whole = (whole * 10.0) + (double)(*cursor - '0');
        ++cursor;
    }
    if (*cursor == '.') {
        ++cursor;
        fraction = crt_parse_decimal_fraction(cursor, &cursor, &exponent);
    }
    if (*cursor == 'e' || *cursor == 'E') {
        ++cursor;
        if (*cursor == '+' || *cursor == '-') {
            exponent_sign = (*cursor == '-') ? -1L : 1L;
            ++cursor;
        }
        while (crt_ascii_isdigit((unsigned char)*cursor)) {
            exponent = (exponent * 10L) + (*cursor - '0');
            ++cursor;
        }
        exponent *= exponent_sign;
    }

    if (endptr != NULL) {
        *endptr = (char*)cursor;
    }

    result = whole + fraction;
    if (exponent != 0L) {
        result *= crt_pow_int(10.0, exponent);
    }

    return negative ? -result : result;
}

int abs(int value) {
    return value < 0 ? -value : value;
}

long labs(long value) {
    return value < 0L ? -value : value;
}

div_t div(int numerator, int denominator) {
    div_t result;
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    return result;
}

ldiv_t ldiv(long numerator, long denominator) {
    ldiv_t result;
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    return result;
}

double sin(double value) {
    double term = value;
    double result = value;

    for (int n = 3, sign = -1; n < 15; n += 2, sign = -sign) {
        term *= value * value / (double)(n * (n - 1));
        result += sign * term;
    }

    return result;
}

double cos(double value) {
    double term = 1.0;
    double result = 1.0;

    for (int n = 2, sign = -1; n < 14; n += 2, sign = -sign) {
        term *= value * value / (double)(n * (n - 1));
        result += sign * term;
    }

    return result;
}

double sqrt(double value) {
    double guess;

    if (value <= 0.0) {
        return 0.0;
    }

    guess = value > 1.0 ? value : 1.0;
    for (int iteration = 0; iteration < 20; ++iteration) {
        guess = 0.5 * (guess + (value / guess));
    }
    return guess;
}

double pow(double base, double exponent) {
    long integer_exponent = (long)exponent;
    if ((double)integer_exponent == exponent) {
        return crt_pow_int(base, integer_exponent);
    }
    return 0.0;
}

FILE* fopen(const char* path, const char* mode) {
    return crt_file_from_path_mode(path, mode);
}

int fclose(FILE* stream) {
    CrtFile* file = crt_stream_from_FILE(stream);

    if (file == NULL) {
        return -1;
    }
    if (file->buffer != NULL) {
        free(file->buffer);
    }
    crt_free_stream(stream);
    return 0;
}

size_t fread(void* buffer, size_t size, size_t count, FILE* stream) {
    return crt_file_read(buffer, size, count, stream);
}

size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream) {
    return crt_file_write(buffer, size, count, stream);
}

int fflush(FILE* stream) { return crt_file_flush(stream); }
int feof(FILE* stream) { CrtFile* file = crt_stream_from_FILE(stream); return file != NULL ? file->eof : 1; }
int ferror(FILE* stream) { CrtFile* file = crt_stream_from_FILE(stream); return file != NULL ? file->error : 1; }
void clearerr(FILE* stream) { crt_clear_error_flags(stream); }
int fseek(FILE* stream, long offset, int origin) { return crt_file_seek(stream, offset, origin); }
long ftell(FILE* stream) { return crt_file_tell(stream); }
void rewind(FILE* stream) { (void)crt_file_seek(stream, 0L, 0); }
int setvbuf(FILE* stream, char* buffer, int mode, size_t size) { (void)stream; (void)buffer; (void)mode; (void)size; return 0; }
int fgetc(FILE* stream) { unsigned char ch = 0U; return fread(&ch, 1U, 1U, stream) == 1U ? (int)ch : EOF; }
int getc(FILE* stream) { return fgetc(stream); }
int fputc(int value, FILE* stream) { unsigned char ch = (unsigned char)value; return fwrite(&ch, 1U, 1U, stream) == 1U ? value : EOF; }
int putc(int value, FILE* stream) { return fputc(value, stream); }
char* fgets(char* buffer, int size, FILE* stream) {
    int ch;
    int index = 0;

    if (buffer == NULL || size <= 0 || stream == NULL) {
        return NULL;
    }

    while (index + 1 < size) {
        ch = fgetc(stream);
        if (ch == EOF) {
            break;
        }
        buffer[index++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }

    if (index == 0) {
        return NULL;
    }

    buffer[index] = '\0';
    return buffer;
}

int puts(const char* text) {
    if (writeText(text != NULL ? text : "(null)") < 0) {
        return -1;
    }
    if (writeLine("") < 0) {
        return -1;
    }
    return 0;
}

static int crt_vformat_to_buffer(char* buffer, size_t size, const char* fmt, va_list args) {
    FormatSink sink;

    sink.buffer = buffer;
    sink.capacity = size;
    sink.length = 0U;
    return format_vprintf(&sink, fmt, args);
}

int vprintf(const char* fmt, va_list args) {
    char buffer[CRT_STDIO_BUFFER_SIZE];
    int written = crt_vformat_to_buffer(buffer, sizeof(buffer), fmt, args);

    if (writeText(buffer) < 0) {
        return -1;
    }
    return written;
}

int printf(const char* fmt, ...) {
    va_list args;
    int written;

    va_start(args, fmt);
    written = vprintf(fmt, args);
    va_end(args);
    return written;
}

int vfprintf(FILE* stream, const char* fmt, va_list args) {
    char buffer[CRT_STDIO_BUFFER_SIZE];
    int written = crt_vformat_to_buffer(buffer, sizeof(buffer), fmt, args);

    if (stream == NULL) {
        return -1;
    }

    return crt_write_file_text(stream, buffer) < 0 ? -1 : written;
}

int fprintf(FILE* stream, const char* fmt, ...) {
    va_list args;
    int written;

    va_start(args, fmt);
    written = vfprintf(stream, fmt, args);
    va_end(args);
    return written;
}

int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args) {
    return crt_vformat_to_buffer(buffer, size, fmt, args);
}

int snprintf(char* buffer, size_t size, const char* fmt, ...) {
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return written;
}

int vsprintf(char* buffer, const char* fmt, va_list args) {
    return crt_vformat_to_buffer(buffer, (size_t)-1, fmt, args);
}

int sprintf(char* buffer, const char* fmt, ...) {
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsprintf(buffer, fmt, args);
    va_end(args);
    return written;
}

int vscanf(const char* fmt, va_list args) { (void)fmt; (void)args; return -1; }
int scanf(const char* fmt, ...) { (void)fmt; return -1; }
int vfscanf(FILE* stream, const char* fmt, va_list args) { (void)stream; (void)fmt; (void)args; return -1; }
int fscanf(FILE* stream, const char* fmt, ...) { (void)stream; (void)fmt; return -1; }
int vsscanf(const char* text, const char* fmt, va_list args) { (void)text; (void)fmt; (void)args; return -1; }
int sscanf(const char* text, const char* fmt, ...) { (void)text; (void)fmt; return -1; }

crt_signal_handler_fn signal(int signum, crt_signal_handler_fn handler) {
    crt_signal_handler_fn previous;

    if (signum < 0 || (unsigned long)signum >= CRT_SIGNAL_COUNT) {
        return SIG_ERR;
    }

    previous = g_signal_handlers[signum].handler;
    g_signal_handlers[signum].handler = handler;
    return previous;
}

int raise(int signum) {
    return crt_signal_trampoline(signum);
}

int setjmp(jmp_buf env) {
    if (env == NULL) {
        return -1;
    }
    return __builtin_setjmp((intptr_t*)env);
}

void longjmp(jmp_buf env, int value) {
    if ((env == NULL) || (value == 0)) {
        value = 1;
    }
    (void)value;
    __builtin_longjmp((intptr_t*)env, 1);
}

void qsort(void* base, size_t count, size_t element_size, crt_compar_fn compar) {
    unsigned char* bytes = (unsigned char*)base;

    if (base == NULL || compar == NULL || element_size == 0U || count < 2U) {
        return;
    }

    for (size_t i = 0U; i < count; ++i) {
        for (size_t j = i + 1U; j < count; ++j) {
            unsigned char* lhs = bytes + (i * element_size);
            unsigned char* rhs = bytes + (j * element_size);
            if (compar(lhs, rhs) > 0) {
                unsigned char temp[256];
                size_t remaining = element_size;
                unsigned char* lhs_cursor = lhs;
                unsigned char* rhs_cursor = rhs;

                while (remaining > 0U) {
                    size_t chunk = remaining > sizeof(temp) ? sizeof(temp) : remaining;
                    memcpy(temp, lhs_cursor, chunk);
                    memcpy(lhs_cursor, rhs_cursor, chunk);
                    memcpy(rhs_cursor, temp, chunk);
                    lhs_cursor += chunk;
                    rhs_cursor += chunk;
                    remaining -= chunk;
                }
            }
        }
    }
}

void* bsearch(const void* key, const void* base, size_t count, size_t element_size, crt_compar_fn compar) {
    const unsigned char* bytes = (const unsigned char*)base;
    size_t low = 0U;
    size_t high = count;

    if (key == NULL || base == NULL || compar == NULL || element_size == 0U) {
        return NULL;
    }

    while (low < high) {
        size_t mid = low + ((high - low) / 2U);
        const void* element = bytes + (mid * element_size);
        int compare = compar(key, element);

        if (compare == 0) {
            return (void*)element;
        }
        if (compare < 0) {
            high = mid;
        }
        else {
            low = mid + 1U;
        }
    }

    return NULL;
}

time_t time(time_t* result) {
    time_t now = (time_t)(getUptimeMs() / 1000UL);

    if (result != NULL) {
        *result = now;
    }
    return now;
}

clock_t clock(void) {
    return (clock_t)getUptimeMs();
}

tm* gmtime(const time_t* value) {
    static tm out;

    crt_time_to_tm((long)(value != NULL ? *value : 0), &out);
    return &out;
}

tm* localtime(const time_t* value) {
    return gmtime(value);
}

time_t mktime(tm* value) {
    return (time_t)crt_tm_to_time(value);
}

size_t strftime(char* buffer, size_t size, const char* format, const tm* time_info) {
    size_t written = 0U;
    char temp[64];
    const char* cursor = format;
    const tm* tm_info = time_info;

    if (buffer == NULL || size == 0U || format == NULL || tm_info == NULL) {
        return 0U;
    }

    while (*cursor != '\0' && written + 1U < size) {
        if (*cursor != '%') {
            buffer[written++] = *cursor++;
            continue;
        }

        ++cursor;
        switch (*cursor) {
        case 'Y':
            snprintf(temp, sizeof(temp), "%04d", tm_info->tm_year + 1900);
            break;
        case 'm':
            snprintf(temp, sizeof(temp), "%02d", tm_info->tm_mon + 1);
            break;
        case 'd':
            snprintf(temp, sizeof(temp), "%02d", tm_info->tm_mday);
            break;
        case 'H':
            snprintf(temp, sizeof(temp), "%02d", tm_info->tm_hour);
            break;
        case 'M':
            snprintf(temp, sizeof(temp), "%02d", tm_info->tm_min);
            break;
        case 'S':
            snprintf(temp, sizeof(temp), "%02d", tm_info->tm_sec);
            break;
        case '%':
            temp[0] = '%';
            temp[1] = '\0';
            break;
        default:
            temp[0] = '%';
            temp[1] = *cursor;
            temp[2] = '\0';
            break;
        }

        for (size_t index = 0U; temp[index] != '\0' && written + 1U < size; ++index) {
            buffer[written++] = temp[index];
        }
        if (*cursor != '\0') {
            ++cursor;
        }
    }

    buffer[written] = '\0';
    return written;
}

size_t wcslen(const wchar_t* text) {
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }

    while (text[length] != L'\0') {
        ++length;
    }

    return length;
}

int wcscmp(const wchar_t* lhs, const wchar_t* rhs) {
    size_t index = 0U;

    if (lhs == rhs) {
        return 0;
    }
    if (lhs == NULL) {
        return -1;
    }
    if (rhs == NULL) {
        return 1;
    }

    while (lhs[index] != L'\0' && rhs[index] != L'\0') {
        if (lhs[index] != rhs[index]) {
            return lhs[index] < rhs[index] ? -1 : 1;
        }
        ++index;
    }

    if (lhs[index] == rhs[index]) {
        return 0;
    }
    return lhs[index] == L'\0' ? -1 : 1;
}

int wcsncmp(const wchar_t* lhs, const wchar_t* rhs, size_t size) {
    size_t index = 0U;

    if (lhs == rhs) {
        return 0;
    }
    if (lhs == NULL) {
        return -1;
    }
    if (rhs == NULL) {
        return 1;
    }

    while (index < size && lhs[index] != L'\0' && rhs[index] != L'\0') {
        if (lhs[index] != rhs[index]) {
            return lhs[index] < rhs[index] ? -1 : 1;
        }
        ++index;
    }

    return 0;
}

wchar_t* wcscpy(wchar_t* destination, const wchar_t* source) {
    size_t index = 0U;

    if (destination == NULL || source == NULL) {
        return destination;
    }

    while ((destination[index] = source[index]) != L'\0') {
        ++index;
    }
    return destination;
}

wchar_t* wcsncpy(wchar_t* destination, const wchar_t* source, size_t size) {
    size_t index = 0U;

    if (destination == NULL || size == 0U) {
        return destination;
    }
    if (source == NULL) {
        destination[0] = L'\0';
        return destination;
    }

    while (index < size && source[index] != L'\0') {
        destination[index] = source[index];
        ++index;
    }
    while (index < size) {
        destination[index++] = L'\0';
    }

    return destination;
}

wchar_t* wcschr(const wchar_t* text, wchar_t value) {
    if (text == NULL) {
        return NULL;
    }

    while (*text != L'\0') {
        if (*text == value) {
            return (wchar_t*)text;
        }
        ++text;
    }

    return value == L'\0' ? (wchar_t*)text : NULL;
}

wchar_t* wcsrchr(const wchar_t* text, wchar_t value) {
    const wchar_t* match = NULL;

    if (text == NULL) {
        return NULL;
    }

    while (*text != L'\0') {
        if (*text == value) {
            match = text;
        }
        ++text;
    }

    if (value == L'\0') {
        return (wchar_t*)text;
    }
    return (wchar_t*)match;
}

void* wmemcpy(void* destination, const void* source, size_t size) {
    return memcpy(destination, source, size * sizeof(wchar_t));
}

void* wmemset(void* destination, wchar_t value, size_t size) {
    wchar_t* out = (wchar_t*)destination;

    for (size_t index = 0U; index < size; ++index) {
        out[index] = value;
    }
    return destination;
}

int wmemcmp(const void* lhs, const void* rhs, size_t size) {
    return memcmp(lhs, rhs, size * sizeof(wchar_t));
}

size_t mbstowcs(wchar_t* destination, const char* source, size_t max_length) {
    size_t written = 0U;

    if (source == NULL) {
        return 0U;
    }
    if (destination == NULL) {
        return strlen(source);
    }

    while (written < max_length && source[written] != '\0') {
        destination[written] = (wchar_t)(unsigned char)source[written];
        ++written;
    }
    if (written < max_length) {
        destination[written] = L'\0';
    }
    return written;
}

size_t wcstombs(char* destination, const wchar_t* source, size_t max_length) {
    size_t written = 0U;

    if (source == NULL) {
        return 0U;
    }
    if (destination == NULL) {
        return wcslen(source);
    }

    while (written < max_length && source[written] != L'\0') {
        destination[written] = (char)(source[written] & 0x7f);
        ++written;
    }
    if (written < max_length) {
        destination[written] = '\0';
    }
    return written;
}

uintptr_t __stack_chk_guard(void) {
    return g_stack_chk_guard;
}

void __stack_chk_fail(void) {
    abort();
}

int __cxa_atexit(void (*function)(void*), void* argument, void* dso_handle) {
    (void)function;
    (void)argument;
    (void)dso_handle;
    return 0;
}

void __cxa_finalize(void* dso_handle) {
    (void)dso_handle;
}

int crt_entry(void* image_base, U32 reason) {
    (void)image_base;
    (void)reason;

    crt_initialize_locale();
    g_standard_input = NULL;
    g_standard_output = NULL;
    g_standard_error = NULL;
    return 1;
}

#ifndef ROS_DLL_EXPORT
#define ROS_DLL_EXPORT DLL_EXPORT
#endif

ROS_DLL_EXPORT(crt_main);
ROS_DLL_EXPORT(atexit);
ROS_DLL_EXPORT(exit);
ROS_DLL_EXPORT(abort);
ROS_DLL_EXPORT(getenv);
ROS_DLL_EXPORT(setenv);
ROS_DLL_EXPORT(unsetenv);
ROS_DLL_EXPORT(clearenv);
ROS_DLL_EXPORT(malloc);
ROS_DLL_EXPORT(free);
ROS_DLL_EXPORT(calloc);
ROS_DLL_EXPORT(realloc);
ROS_DLL_EXPORT(aligned_alloc);
ROS_DLL_EXPORT(memcpy);
ROS_DLL_EXPORT(memmove);
ROS_DLL_EXPORT(memset);
ROS_DLL_EXPORT(memcmp);
ROS_DLL_EXPORT(strlen);
ROS_DLL_EXPORT(strnlen);
ROS_DLL_EXPORT(strcmp);
ROS_DLL_EXPORT(strncmp);
ROS_DLL_EXPORT(strcpy);
ROS_DLL_EXPORT(strncpy);
ROS_DLL_EXPORT(strchr);
ROS_DLL_EXPORT(strstr);
ROS_DLL_EXPORT(isalpha);
ROS_DLL_EXPORT(isdigit);
ROS_DLL_EXPORT(isspace);
ROS_DLL_EXPORT(tolower);
ROS_DLL_EXPORT(toupper);
ROS_DLL_EXPORT(setlocale);
ROS_DLL_EXPORT(localeconv);
ROS_DLL_EXPORT(strtol);
ROS_DLL_EXPORT(strtoul);
ROS_DLL_EXPORT(strtod);
ROS_DLL_EXPORT(abs);
ROS_DLL_EXPORT(labs);
ROS_DLL_EXPORT(div);
ROS_DLL_EXPORT(ldiv);
ROS_DLL_EXPORT(sin);
ROS_DLL_EXPORT(cos);
ROS_DLL_EXPORT(sqrt);
ROS_DLL_EXPORT(pow);
ROS_DLL_EXPORT(fopen);
ROS_DLL_EXPORT(fclose);
ROS_DLL_EXPORT(fread);
ROS_DLL_EXPORT(fwrite);
ROS_DLL_EXPORT(fflush);
ROS_DLL_EXPORT(feof);
ROS_DLL_EXPORT(ferror);
ROS_DLL_EXPORT(clearerr);
ROS_DLL_EXPORT(fseek);
ROS_DLL_EXPORT(ftell);
ROS_DLL_EXPORT(rewind);
ROS_DLL_EXPORT(setvbuf);
ROS_DLL_EXPORT(fgetc);
ROS_DLL_EXPORT(getc);
ROS_DLL_EXPORT(fputc);
ROS_DLL_EXPORT(putc);
ROS_DLL_EXPORT(fgets);
ROS_DLL_EXPORT(puts);
ROS_DLL_EXPORT(vprintf);
ROS_DLL_EXPORT(printf);
ROS_DLL_EXPORT(vfprintf);
ROS_DLL_EXPORT(fprintf);
ROS_DLL_EXPORT(vsnprintf);
ROS_DLL_EXPORT(snprintf);
ROS_DLL_EXPORT(vsprintf);
ROS_DLL_EXPORT(sprintf);
ROS_DLL_EXPORT(vscanf);
ROS_DLL_EXPORT(scanf);
ROS_DLL_EXPORT(vfscanf);
ROS_DLL_EXPORT(fscanf);
ROS_DLL_EXPORT(vsscanf);
ROS_DLL_EXPORT(sscanf);
ROS_DLL_EXPORT(signal);
ROS_DLL_EXPORT(raise);
ROS_DLL_EXPORT(setjmp);
ROS_DLL_EXPORT(longjmp);
ROS_DLL_EXPORT(qsort);
ROS_DLL_EXPORT(bsearch);
ROS_DLL_EXPORT(time);
ROS_DLL_EXPORT(clock);
ROS_DLL_EXPORT(gmtime);
ROS_DLL_EXPORT(localtime);
ROS_DLL_EXPORT(mktime);
ROS_DLL_EXPORT(strftime);
ROS_DLL_EXPORT(mbstowcs);
ROS_DLL_EXPORT(wcstombs);
ROS_DLL_EXPORT(wcslen);
ROS_DLL_EXPORT(wcscmp);
ROS_DLL_EXPORT(wcsncmp);
ROS_DLL_EXPORT(wcscpy);
ROS_DLL_EXPORT(wcsncpy);
ROS_DLL_EXPORT(wcschr);
ROS_DLL_EXPORT(wcsrchr);
ROS_DLL_EXPORT(wmemcpy);
ROS_DLL_EXPORT(wmemset);
ROS_DLL_EXPORT(wmemcmp);
ROS_DLL_EXPORT(__stack_chk_guard);
ROS_DLL_EXPORT(__stack_chk_fail);
ROS_DLL_EXPORT(__cxa_atexit);
ROS_DLL_EXPORT(__cxa_finalize);
