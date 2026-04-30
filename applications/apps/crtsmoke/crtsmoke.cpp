#define ROS_APP_WITH_CRT 1
#include "app/app.h"

#include <stdint.h>

#define CRTSMOKE_PUTS_OK(text) ((crt_puts(text) >= 0) ? 1 : 0)
#define CRTSMOKE_ENV_STRESS_COUNT 48UL

static int g_signal_count;
static int g_atexit_count;

/*
 * Build one deterministic environment-variable name for the stress loop.
 *
 * Reusing the same stack buffer on every iteration verifies that `setenv`
 * copies caller-provided names instead of storing transient pointers.
 *
 * @param buffer Receives the generated variable name.
 * @param index Zero-based stress-loop index.
 * @return Nothing.
 */
static void crtsmoke_build_env_name(char* buffer, unsigned long index) {
    if (buffer == NULL) {
        return;
    }

    (void)crt_snprintf(buffer, 32U, "CRT_SMOKE_%lu", index);
}

/*
 * Build one deterministic environment-variable value for the stress loop.
 *
 * Using a separate generated value lets the test catch both the old registry
 * ceiling and any future regression where the CRT keeps borrowed stack-backed
 * pointers instead of private copies.
 *
 * @param buffer Receives the generated variable value.
 * @param index Zero-based stress-loop index.
 * @return Nothing.
 */
static void crtsmoke_build_env_value(char* buffer, unsigned long index) {
    if (buffer == NULL) {
        return;
    }

    (void)crt_snprintf(buffer, 32U, "value-%lu", index);
}

static void crtsmoke_atexit_handler(void) {
    ++g_atexit_count;
    (void)crt_puts("crtsmoke.exe: atexit handler ran");
}

static void crtsmoke_signal_handler(int signum) {
    ++g_signal_count;
    (void)signum;
}

static int crtsmoke_compare_long(const void* lhs, const void* rhs) {
    const long left = *(const long*)lhs;
    const long right = *(const long*)rhs;

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static int crtsmoke_expect(int condition, const char* label) {
    if (!condition) {
        (void)crt_puts(label);
        return 0;
    }
    return 1;
}

static int crtsmoke_expect_text(const char* label, const char* expected, const char* actual) {
    if (crt_strcmp(expected, actual) != 0) {
        char line[256];

        crt_snprintf(line, sizeof(line), "%s expected='%s' actual='%s'", label, expected, actual ? actual : "<null>");
        (void)crt_puts(line);
        return 0;
    }

    return 1;
}

static int crtsmoke_expect_long(const char* label, long expected, long actual) {
    if (expected != actual) {
        char line[128];

        crt_snprintf(line, sizeof(line), "%s expected=%ld actual=%ld", label, expected, actual);
        (void)crt_puts(line);
        return 0;
    }

    return 1;
}

static int crtsmoke_expect_ulong(const char* label, unsigned long expected, unsigned long actual) {
    if (expected != actual) {
        char line[128];

        crt_snprintf(line, sizeof(line), "%s expected=%lu actual=%lu", label, expected, actual);
        (void)crt_puts(line);
        return 0;
    }

    return 1;
}

static int crtsmoke_expect_double(const char* label, double expected, double actual, double tolerance) {
    double delta = actual - expected;

    if (delta < 0.0) {
        delta = -delta;
    }
    if (delta > tolerance) {
        char line[128];

        crt_snprintf(line, sizeof(line), "%s expected=%.6f actual=%.6f", label, expected, actual);
        (void)crt_puts(line);
        return 0;
    }

    return 1;
}

static int crtsmoke_run_signal_test(void) {
    crt_signal_handler_fn previous;

    previous = crt_signal(SIGINT, crtsmoke_signal_handler);
    (void)previous;
    if (crt_raise(SIGINT) != 0) {
        return crtsmoke_expect(0, "signal/raise failed");
    }

    return crtsmoke_expect_long("signal count", 1, g_signal_count);
}

static int crtsmoke_run_environment_test(void) {
    const char* value;
    int ok = 1;
    unsigned long index;

    ok &= crtsmoke_expect(crt_setenv("CRT_SMOKE_VALUE", "alpha", 1) == 0, "setenv failed");
    value = crt_getenv("CRT_SMOKE_VALUE");
    ok &= crtsmoke_expect_text("getenv", "alpha", value);
    ok &= crtsmoke_expect(crt_unsetenv("CRT_SMOKE_VALUE") == 0, "unsetenv failed");
    ok &= crtsmoke_expect(crt_getenv("CRT_SMOKE_VALUE") == NULL, "unsetenv did not clear value");
    ok &= crtsmoke_expect(crt_setenv("CRT_SMOKE_VALUE", "beta", 1) == 0, "setenv failed again");

    for (index = 0UL; index < CRTSMOKE_ENV_STRESS_COUNT; ++index) {
        char name[32];
        char expected[32];

        crtsmoke_build_env_name(name, index);
        crtsmoke_build_env_value(expected, index);
        ok &= crtsmoke_expect(crt_setenv(name, expected, 1) == 0, "stress setenv failed");
    }

    for (index = 0UL; index < CRTSMOKE_ENV_STRESS_COUNT; ++index) {
        char label[64];
        char name[32];
        char expected[32];

        crtsmoke_build_env_name(name, index);
        crtsmoke_build_env_value(expected, index);
        (void)crt_snprintf(label, sizeof(label), "stress getenv %lu", index);
        ok &= crtsmoke_expect_text(label, expected, crt_getenv(name));
    }

    ok &= crtsmoke_expect(crt_clearenv() == 0, "clearenv failed");
    ok &= crtsmoke_expect(crt_getenv("CRT_SMOKE_VALUE") == NULL, "clearenv did not clear value");

    for (index = 0UL; index < CRTSMOKE_ENV_STRESS_COUNT; ++index) {
        char name[32];

        crtsmoke_build_env_name(name, index);
        ok &= crtsmoke_expect(crt_getenv(name) == NULL, "clearenv did not clear stress values");
    }

    return ok;
}

static int crtsmoke_run_memory_test(void) {
    unsigned char* block = (unsigned char*)crt_malloc(32);
    unsigned char* resized;
    void* aligned;
    unsigned long expected_pattern;
    int ok = 1;

    ok &= crtsmoke_expect(block != NULL, "malloc failed");
    if (!ok) {
        return 0;
    }

    crt_memset(block, 0x11, 32);
    expected_pattern = 0UL;
    crt_memset(&expected_pattern, 0x11, sizeof(expected_pattern));
    ok &= crtsmoke_expect_ulong("memset/memcpy", expected_pattern, *(unsigned long*)block);
    crt_memcpy(block + 4, "ABCD", 5);
    ok &= crtsmoke_expect_text("memcpy", "ABCD", (const char*)(block + 4));
    crt_memmove(block + 6, block + 4, 5);
    ok &= crtsmoke_expect_text("memmove", "ABCD", (const char*)(block + 6));
    ok &= crtsmoke_expect(crt_memcmp("abc", "abc", 3) == 0, "memcmp equality failed");
    ok &= crtsmoke_expect(crt_strlen("hello") == 5U, "strlen failed");
    ok &= crtsmoke_expect(crt_strnlen("hello", 3U) == 3U, "strnlen failed");
    ok &= crtsmoke_expect(crt_strcmp("alpha", "alpha") == 0, "strcmp failed");
    ok &= crtsmoke_expect(crt_strncmp("alphabet", "alpha", 5U) == 0, "strncmp failed");
    ok &= crtsmoke_expect_text("strchr", "llo", crt_strchr("hello", 'l'));
    ok &= crtsmoke_expect_text("strstr", "world", crt_strstr("hello world", "world"));

    resized = (unsigned char*)crt_realloc(block, 64);
    ok &= crtsmoke_expect(resized != NULL, "realloc failed");
    if (resized != NULL) {
        block = resized;
    }
    crt_free(block);

    aligned = crt_aligned_alloc(32U, 64U);
    ok &= crtsmoke_expect(aligned != NULL, "aligned_alloc failed");
    crt_free(aligned);

    return ok;
}

static int crtsmoke_run_classification_test(void) {
    int ok = 1;

    ok &= crtsmoke_expect(crt_isalpha('A') != 0, "isalpha failed");
    ok &= crtsmoke_expect(crt_isdigit('9') != 0, "isdigit failed");
    ok &= crtsmoke_expect(crt_isspace(' ') != 0, "isspace failed");
    ok &= crtsmoke_expect(crt_tolower('Q') == 'q', "tolower failed");
    ok &= crtsmoke_expect(crt_toupper('q') == 'Q', "toupper failed");
    return ok;
}

static int crtsmoke_run_numeric_test(void) {
    char* endptr;
    long signed_value;
    unsigned long unsigned_value;
    double floating_value;
    int ok = 1;

    signed_value = crt_strtol("-42", &endptr, 10);
    ok &= crtsmoke_expect_long("strtol", -42, signed_value);
    ok &= crtsmoke_expect(*endptr == '\0', "strtol endptr failed");

    unsigned_value = crt_strtoul("2a", &endptr, 16);
    ok &= crtsmoke_expect_ulong("strtoul", 42UL, unsigned_value);
    ok &= crtsmoke_expect(*endptr == '\0', "strtoul endptr failed");

    floating_value = crt_strtod("12.5", &endptr);
    ok &= crtsmoke_expect_double("strtod", 12.5, floating_value, 0.0001);
    ok &= crtsmoke_expect(*endptr == '\0', "strtod endptr failed");

    ok &= crtsmoke_expect_long("abs", 7, crt_abs(-7));
    ok &= crtsmoke_expect_long("labs", 9, crt_labs(-9L));
    ok &= crtsmoke_expect_long("div", 2, crt_div(7, 3).quot);
    ok &= crtsmoke_expect_long("div rem", 1, crt_div(7, 3).rem);
    ok &= crtsmoke_expect_long("ldiv", 3, crt_ldiv(10L, 3L).quot);
    ok &= crtsmoke_expect_long("ldiv rem", 1, crt_ldiv(10L, 3L).rem);
    ok &= crtsmoke_expect_double("sin", 0.0, crt_sin(0.0), 0.0001);
    ok &= crtsmoke_expect_double("cos", 1.0, crt_cos(0.0), 0.0001);
    ok &= crtsmoke_expect_double("sqrt", 3.0, crt_sqrt(9.0), 0.01);
    ok &= crtsmoke_expect_double("pow", 8.0, crt_pow(2.0, 3.0), 0.0001);
    return ok;
}

static int crtsmoke_run_sort_test(void) {
    long values[] = { 4, 1, 3, 2 };
    long* found;
    long target = 3;

    crt_qsort(values, 4U, sizeof(values[0]), crtsmoke_compare_long);
    if (!(values[0] == 1 && values[1] == 2 && values[2] == 3 && values[3] == 4)) {
        (void)crt_puts("qsort failed");
        return 0;
    }

    found = (long*)crt_bsearch(&target, values, 4U, sizeof(values[0]), crtsmoke_compare_long);
    if (found == NULL || *found != 3) {
        (void)crt_puts("bsearch failed");
        return 0;
    }

    return 1;
}

static int crtsmoke_run_wide_test(void) {
    wchar_t wide_text[32];
    char narrow_text[32];
    int ok = 1;

    ok &= crtsmoke_expect(crt_mbstowcs(wide_text, "wide", 32U) == 4U, "mbstowcs failed");
    ok &= crtsmoke_expect(crt_wcslen(wide_text) == 4U, "wcslen failed");
    ok &= crtsmoke_expect(crt_wcscmp(wide_text, wide_text) == 0, "wcscmp failed");
    ok &= crtsmoke_expect(crt_wcschr(wide_text, L'i') != NULL, "wcschr failed");
    ok &= crtsmoke_expect(crt_wcstombs(narrow_text, wide_text, 32U) == 4U, "wcstombs failed");
    ok &= crtsmoke_expect_text("wcstombs", "wide", narrow_text);

    return ok;
}

static int crtsmoke_run_time_test(void) {
    time_t now;
    tm* current;
    tm tm_value;
    char buffer[64];
    int ok = 1;

    now = crt_time(NULL);
    current = crt_gmtime(&now);
    ok &= crtsmoke_expect(current != NULL, "gmtime failed");
    if (current != NULL) {
        ok &= crtsmoke_expect(crt_mktime(current) == now, "mktime failed");
        ok &= crtsmoke_expect(crt_strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", current) > 0U, "strftime failed");
    }

    tm_value.tm_year = 124;
    tm_value.tm_mon = 0;
    tm_value.tm_mday = 1;
    tm_value.tm_hour = 0;
    tm_value.tm_min = 0;
    tm_value.tm_sec = 0;
    tm_value.tm_isdst = 0;
    ok &= crtsmoke_expect(crt_mktime(&tm_value) >= 0, "mktime custom failed");
    ok &= crtsmoke_expect(crt_clock() >= 0, "clock failed");
    return ok;
}

static int crtsmoke_run_stdio_test(void) {
    FILE* stream;
    char buffer[128];
    char readback[128];
    int ok = 1;
    size_t bytes_written;
    size_t bytes_read;

    ok &= crtsmoke_expect(crt_snprintf(buffer, sizeof(buffer), "value=%d", 42) > 0, "snprintf failed");
    ok &= crtsmoke_expect_text("snprintf", "value=42", buffer);
    stream = crt_fopen("crtsmoke-buffer", "w+");
    ok &= crtsmoke_expect(stream != NULL, "fopen failed");
    if (stream == NULL) {
        return 0;
    }

    bytes_written = crt_fwrite("alpha", 1U, 5U, stream);
    ok &= crtsmoke_expect(bytes_written == 5U, "fwrite failed");
    ok &= crtsmoke_expect(crt_fflush(stream) == 0, "fflush failed");
    ok &= crtsmoke_expect(crt_fseek(stream, 0L, 0) == 0, "fseek failed");
    bytes_read = crt_fread(readback, 1U, 5U, stream);
    readback[bytes_read] = '\0';
    ok &= crtsmoke_expect(bytes_read == 5U, "fread failed");
    ok &= crtsmoke_expect_text("file roundtrip", "alpha", readback);
    ok &= crtsmoke_expect(crt_ftell(stream) == 5L, "ftell failed");
    ok &= crtsmoke_expect(crt_feof(stream) == 0, "feof failed");
    ok &= crtsmoke_expect(crt_ferror(stream) == 0, "ferror failed");
    crt_clearerr(stream);
    ok &= crtsmoke_expect(crt_fclose(stream) == 0, "fclose failed");

    return ok;
}

static int crtsmoke_run_misc_output_test(void) {
    int ok = 1;

    ok &= crtsmoke_expect(CRTSMOKE_PUTS_OK("crtsmoke.exe: puts exercised"), "puts failed");
    ok &= crtsmoke_expect(crt_printf("crtsmoke.exe: printf exercised %d\n", 1) >= 0, "printf failed");
    return ok;
}

static int crtsmoke_run_reported_test(const char* name, int (*fn)(void)) {
    int result;

    crt_printf("crtsmoke.exe: begin %s\n", (name != NULL) ? name : "<test>");
    result = (fn != NULL) ? fn() : 0;
    crt_printf("crtsmoke.exe: %s %s\n", (name != NULL) ? name : "<test>", result ? "ok" : "failed");
    return result;
}

int main(void) {
    int ok = 1;

    ok &= crtsmoke_expect(crt_at_exit(crtsmoke_atexit_handler) == 0, "atexit registration failed");
    ok &= crtsmoke_run_reported_test("environment", crtsmoke_run_environment_test);
    ok &= crtsmoke_run_reported_test("memory", crtsmoke_run_memory_test);
    ok &= crtsmoke_run_reported_test("classification", crtsmoke_run_classification_test);
    ok &= crtsmoke_run_reported_test("numeric", crtsmoke_run_numeric_test);
    ok &= crtsmoke_run_reported_test("sort", crtsmoke_run_sort_test);
    ok &= crtsmoke_run_reported_test("wide", crtsmoke_run_wide_test);
    ok &= crtsmoke_run_reported_test("time", crtsmoke_run_time_test);
    ok &= crtsmoke_run_reported_test("stdio", crtsmoke_run_stdio_test);
    ok &= crtsmoke_run_reported_test("signal", crtsmoke_run_signal_test);
    ok &= crtsmoke_run_reported_test("misc", crtsmoke_run_misc_output_test);

    if (!ok) {
        (void)crt_puts("crtsmoke.exe: failed");
        crt_exit(1);
    }

    (void)crt_puts("crtsmoke.exe: success");
    crt_exit(0xBEEFDEAD);
    return 0xDEADBEEF;
}
