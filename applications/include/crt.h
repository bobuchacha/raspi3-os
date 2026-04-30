#ifndef APPLICATIONS_CRT_H
#define APPLICATIONS_CRT_H

#include "user_runtime.h"
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRT_CLIENT_MODULE_NAME "crt.dll"

#ifndef EOF
#define EOF (-1)
#endif

#ifndef SIG_DFL
#define SIG_DFL ((void (*)(int))0)
#endif
#ifndef SIG_IGN
#define SIG_IGN ((void (*)(int))1)
#endif
#ifndef SIG_ERR
#define SIG_ERR ((void (*)(int))-1)
#endif

#ifndef SIGABRT
#define SIGABRT 6
#endif
#ifndef SIGFPE
#define SIGFPE 8
#endif
#ifndef SIGILL
#define SIGILL 4
#endif
#ifndef SIGINT
#define SIGINT 2
#endif
#ifndef SIGSEGV
#define SIGSEGV 11
#endif
#ifndef SIGTERM
#define SIGTERM 15
#endif

#ifndef LC_ALL
#define LC_ALL 0
#endif
#ifndef LC_COLLATE
#define LC_COLLATE 1
#endif
#ifndef LC_CTYPE
#define LC_CTYPE 2
#endif
#ifndef LC_MONETARY
#define LC_MONETARY 3
#endif
#ifndef LC_NUMERIC
#define LC_NUMERIC 4
#endif
#ifndef LC_TIME
#define LC_TIME 5
#endif
#ifndef MB_CUR_MAX
#define MB_CUR_MAX 4U
#endif

    typedef int (*crt_main_fn)(int argc, char** argv);
    typedef void (*crt_atexit_fn)(void);
    typedef void (*crt_signal_handler_fn)(int);
    typedef int (*crt_compar_fn)(const void*, const void*);
    typedef struct FILE FILE;
    typedef intptr_t jmp_buf[5];
    typedef long time_t;
    typedef long clock_t;

    typedef struct tm {
        int tm_sec;
        int tm_min;
        int tm_hour;
        int tm_mday;
        int tm_mon;
        int tm_year;
        int tm_wday;
        int tm_yday;
        int tm_isdst;
    } tm;

    typedef struct lconv {
        char* decimal_point;
        char* thousands_sep;
        char* grouping;
        char* int_curr_symbol;
        char* currency_symbol;
        char* mon_decimal_point;
        char* mon_thousands_sep;
        char* mon_grouping;
        char* positive_sign;
        char* negative_sign;
        char int_frac_digits;
        char frac_digits;
        char p_cs_precedes;
        char p_sep_by_space;
        char n_cs_precedes;
        char n_sep_by_space;
        char p_sign_posn;
        char n_sign_posn;
        char int_p_cs_precedes;
        char int_p_sep_by_space;
        char int_n_cs_precedes;
        char int_n_sep_by_space;
        char int_p_sign_posn;
        char int_n_sign_posn;
    } lconv;

    typedef struct div_t {
        int quot;
        int rem;
    } div_t;

    typedef struct ldiv_t {
        long quot;
        long rem;
    } ldiv_t;

#if defined(CRT_EXPORTS)

    int crt_main(crt_main_fn main_fn, int argc, char** argv);
    int atexit(void (*function)(void));
    void exit(int code) __attribute__((noreturn));
    void abort(void) __attribute__((noreturn));
    char* getenv(const char* name);
    int setenv(const char* name, const char* value, int overwrite);
    int unsetenv(const char* name);
    int clearenv(void);

    void* malloc(size_t size);
    void free(void* ptr);
    void* calloc(size_t count, size_t size);
    void* realloc(void* ptr, size_t size);
    void* aligned_alloc(size_t alignment, size_t size);

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

    int isalpha(int value);
    int isdigit(int value);
    int isspace(int value);
    int tolower(int value);
    int toupper(int value);

    char* setlocale(int category, const char* locale_name);
    lconv* localeconv(void);
    long strtol(const char* text, char** endptr, int base);
    unsigned long strtoul(const char* text, char** endptr, int base);
    double strtod(const char* text, char** endptr);
    int abs(int value);
    long labs(long value);
    div_t div(int numerator, int denominator);
    ldiv_t ldiv(long numerator, long denominator);
    double sin(double value);
    double cos(double value);
    double sqrt(double value);
    double pow(double base, double exponent);

    FILE* fopen(const char* path, const char* mode);
    int fclose(FILE* stream);
    size_t fread(void* buffer, size_t size, size_t count, FILE* stream);
    size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream);
    int fflush(FILE* stream);
    int feof(FILE* stream);
    int ferror(FILE* stream);
    void clearerr(FILE* stream);
    int fseek(FILE* stream, long offset, int origin);
    long ftell(FILE* stream);
    void rewind(FILE* stream);
    int setvbuf(FILE* stream, char* buffer, int mode, size_t size);
    int fgetc(FILE* stream);
    int getc(FILE* stream);
    int fputc(int value, FILE* stream);
    int putc(int value, FILE* stream);
    char* fgets(char* buffer, int size, FILE* stream);
    int puts(const char* text);
    int vprintf(const char* fmt, va_list args);
    int printf(const char* fmt, ...);
    int vfprintf(FILE* stream, const char* fmt, va_list args);
    int fprintf(FILE* stream, const char* fmt, ...);
    int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args);
    int snprintf(char* buffer, size_t size, const char* fmt, ...);
    int vsprintf(char* buffer, const char* fmt, va_list args);
    int sprintf(char* buffer, const char* fmt, ...);
    int vscanf(const char* fmt, va_list args);
    int scanf(const char* fmt, ...);
    int vfscanf(FILE* stream, const char* fmt, va_list args);
    int fscanf(FILE* stream, const char* fmt, ...);
    int vsscanf(const char* text, const char* fmt, va_list args);
    int sscanf(const char* text, const char* fmt, ...);

    crt_signal_handler_fn signal(int signum, crt_signal_handler_fn handler);
    int raise(int signum);
    int setjmp(jmp_buf env);
    void longjmp(jmp_buf env, int value) __attribute__((noreturn));
    void qsort(void* base, size_t count, size_t element_size, crt_compar_fn compar);
    void* bsearch(const void* key, const void* base, size_t count, size_t element_size, crt_compar_fn compar);

    time_t time(time_t* result);
    clock_t clock(void);
    tm* gmtime(const time_t* value);
    tm* localtime(const time_t* value);
    time_t mktime(tm* value);
    size_t strftime(char* buffer, size_t size, const char* format, const tm* time_info);
    size_t mbstowcs(wchar_t* destination, const char* source, size_t max_length);
    size_t wcstombs(char* destination, const wchar_t* source, size_t max_length);
    size_t wcslen(const wchar_t* text);
    int wcscmp(const wchar_t* lhs, const wchar_t* rhs);
    int wcsncmp(const wchar_t* lhs, const wchar_t* rhs, size_t size);
    wchar_t* wcscpy(wchar_t* destination, const wchar_t* source);
    wchar_t* wcsncpy(wchar_t* destination, const wchar_t* source, size_t size);
    wchar_t* wcschr(const wchar_t* text, wchar_t value);
    wchar_t* wcsrchr(const wchar_t* text, wchar_t value);
    void* wmemcpy(void* destination, const void* source, size_t size);
    void* wmemset(void* destination, wchar_t value, size_t size);
    int wmemcmp(const void* lhs, const void* rhs, size_t size);

    uintptr_t __stack_chk_guard(void);
    void __stack_chk_fail(void) __attribute__((noreturn));
    int __cxa_atexit(void (*function)(void*), void* argument, void* dso_handle);
    void __cxa_finalize(void* dso_handle);
    extern void* __dso_handle;

#else

    DECLARE(int, crt_run_main, (crt_main_fn main_fn, int argc, char** argv), FROM, CRT_CLIENT_MODULE_NAME, "crt_main");
    DECLARE(int, crt_at_exit, (void (*function)(void)), FROM, CRT_CLIENT_MODULE_NAME, "atexit");
    DECLARE(void, crt_exit, (int code), FROM, CRT_CLIENT_MODULE_NAME, "exit");
    DECLARE(void, crt_abort, (void), FROM, CRT_CLIENT_MODULE_NAME, "abort");
    DECLARE(char*, crt_getenv, (const char* name), FROM, CRT_CLIENT_MODULE_NAME, "getenv");
    DECLARE(int, crt_setenv, (const char* name, const char* value, int overwrite), FROM, CRT_CLIENT_MODULE_NAME, "setenv");
    DECLARE(int, crt_unsetenv, (const char* name), FROM, CRT_CLIENT_MODULE_NAME, "unsetenv");
    DECLARE(int, crt_clearenv, (void), FROM, CRT_CLIENT_MODULE_NAME, "clearenv");

    DECLARE(void*, crt_malloc, (size_t size), FROM, CRT_CLIENT_MODULE_NAME, "malloc");
    DECLARE(void, crt_free, (void* ptr), FROM, CRT_CLIENT_MODULE_NAME, "free");
    DECLARE(void*, crt_calloc, (size_t count, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "calloc");
    DECLARE(void*, crt_realloc, (void* ptr, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "realloc");
    DECLARE(void*, crt_aligned_alloc, (size_t alignment, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "aligned_alloc");

    DECLARE(void*, crt_memcpy, (void* dest, const void* src, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "memcpy");
    DECLARE(void*, crt_memmove, (void* dest, const void* src, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "memmove");
    DECLARE(void*, crt_memset, (void* dest, int value, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "memset");
    DECLARE(int, crt_memcmp, (const void* lhs, const void* rhs, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "memcmp");
    DECLARE(size_t, crt_strlen, (const char* text), FROM, CRT_CLIENT_MODULE_NAME, "strlen");
    DECLARE(size_t, crt_strnlen, (const char* text, size_t max_length), FROM, CRT_CLIENT_MODULE_NAME, "strnlen");
    DECLARE(int, crt_strcmp, (const char* lhs, const char* rhs), FROM, CRT_CLIENT_MODULE_NAME, "strcmp");
    DECLARE(int, crt_strncmp, (const char* lhs, const char* rhs, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "strncmp");
    DECLARE(char*, crt_strcpy, (char* dest, const char* src), FROM, CRT_CLIENT_MODULE_NAME, "strcpy");
    DECLARE(char*, crt_strncpy, (char* dest, const char* src, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "strncpy");
    DECLARE(char*, crt_strchr, (const char* text, int value), FROM, CRT_CLIENT_MODULE_NAME, "strchr");
    DECLARE(char*, crt_strstr, (const char* haystack, const char* needle), FROM, CRT_CLIENT_MODULE_NAME, "strstr");

    DECLARE(int, crt_isalpha, (int value), FROM, CRT_CLIENT_MODULE_NAME, "isalpha");
    DECLARE(int, crt_isdigit, (int value), FROM, CRT_CLIENT_MODULE_NAME, "isdigit");
    DECLARE(int, crt_isspace, (int value), FROM, CRT_CLIENT_MODULE_NAME, "isspace");
    DECLARE(int, crt_tolower, (int value), FROM, CRT_CLIENT_MODULE_NAME, "tolower");
    DECLARE(int, crt_toupper, (int value), FROM, CRT_CLIENT_MODULE_NAME, "toupper");
    DECLARE(char*, crt_setlocale, (int category, const char* locale_name), FROM, CRT_CLIENT_MODULE_NAME, "setlocale");
    DECLARE(lconv*, crt_localeconv, (void), FROM, CRT_CLIENT_MODULE_NAME, "localeconv");
    DECLARE(long, crt_strtol, (const char* text, char** endptr, int base), FROM, CRT_CLIENT_MODULE_NAME, "strtol");
    DECLARE(unsigned long, crt_strtoul, (const char* text, char** endptr, int base), FROM, CRT_CLIENT_MODULE_NAME, "strtoul");
    DECLARE(double, crt_strtod, (const char* text, char** endptr), FROM, CRT_CLIENT_MODULE_NAME, "strtod");
    DECLARE(int, crt_abs, (int value), FROM, CRT_CLIENT_MODULE_NAME, "abs");
    DECLARE(long, crt_labs, (long value), FROM, CRT_CLIENT_MODULE_NAME, "labs");
    DECLARE(div_t, crt_div, (int numerator, int denominator), FROM, CRT_CLIENT_MODULE_NAME, "div");
    DECLARE(ldiv_t, crt_ldiv, (long numerator, long denominator), FROM, CRT_CLIENT_MODULE_NAME, "ldiv");
    DECLARE(double, crt_sin, (double value), FROM, CRT_CLIENT_MODULE_NAME, "sin");
    DECLARE(double, crt_cos, (double value), FROM, CRT_CLIENT_MODULE_NAME, "cos");
    DECLARE(double, crt_sqrt, (double value), FROM, CRT_CLIENT_MODULE_NAME, "sqrt");
    DECLARE(double, crt_pow, (double base, double exponent), FROM, CRT_CLIENT_MODULE_NAME, "pow");

    DECLARE(FILE*, crt_fopen, (const char* path, const char* mode), FROM, CRT_CLIENT_MODULE_NAME, "fopen");
    DECLARE(int, crt_fclose, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fclose");
    DECLARE(size_t, crt_fread, (void* buffer, size_t size, size_t count, FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fread");
    DECLARE(size_t, crt_fwrite, (const void* buffer, size_t size, size_t count, FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fwrite");
    DECLARE(int, crt_fflush, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fflush");
    DECLARE(int, crt_feof, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "feof");
    DECLARE(int, crt_ferror, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "ferror");
    DECLARE(void, crt_clearerr, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "clearerr");
    DECLARE(int, crt_fseek, (FILE* stream, long offset, int origin), FROM, CRT_CLIENT_MODULE_NAME, "fseek");
    DECLARE(long, crt_ftell, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "ftell");
    DECLARE(void, crt_rewind, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "rewind");
    DECLARE(int, crt_setvbuf, (FILE* stream, char* buffer, int mode, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "setvbuf");
    DECLARE(int, crt_fgetc, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fgetc");
    DECLARE(int, crt_getc, (FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "getc");
    DECLARE(int, crt_fputc, (int value, FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fputc");
    DECLARE(int, crt_putc, (int value, FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "putc");
    DECLARE(char*, crt_fgets, (char* buffer, int size, FILE* stream), FROM, CRT_CLIENT_MODULE_NAME, "fgets");
    DECLARE(int, crt_puts, (const char* text), FROM, CRT_CLIENT_MODULE_NAME, "puts");
    DECLARE(int, crt_vprintf, (const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vprintf");
    DECLARE(int, crt_printf, (const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "printf");
    DECLARE(int, crt_vfprintf, (FILE* stream, const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vfprintf");
    DECLARE(int, crt_fprintf, (FILE* stream, const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "fprintf");
    DECLARE(int, crt_vsnprintf, (char* buffer, size_t size, const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vsnprintf");
    DECLARE(int, crt_snprintf, (char* buffer, size_t size, const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "snprintf");
    DECLARE(int, crt_vsprintf, (char* buffer, const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vsprintf");
    DECLARE(int, crt_sprintf, (char* buffer, const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "sprintf");
    DECLARE(int, crt_vscanf, (const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vscanf");
    DECLARE(int, crt_scanf, (const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "scanf");
    DECLARE(int, crt_vfscanf, (FILE* stream, const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vfscanf");
    DECLARE(int, crt_fscanf, (FILE* stream, const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "fscanf");
    DECLARE(int, crt_vsscanf, (const char* text, const char* fmt, va_list args), FROM, CRT_CLIENT_MODULE_NAME, "vsscanf");
    DECLARE(int, crt_sscanf, (const char* text, const char* fmt, ...), FROM, CRT_CLIENT_MODULE_NAME, "sscanf");

    DECLARE(crt_signal_handler_fn, crt_signal, (int signum, crt_signal_handler_fn handler), FROM, CRT_CLIENT_MODULE_NAME, "signal");
    DECLARE(int, crt_raise, (int signum), FROM, CRT_CLIENT_MODULE_NAME, "raise");
    DECLARE(int, crt_setjmp, (jmp_buf env), FROM, CRT_CLIENT_MODULE_NAME, "setjmp");
    DECLARE(void, crt_longjmp, (jmp_buf env, int value), FROM, CRT_CLIENT_MODULE_NAME, "longjmp");
    DECLARE(void, crt_qsort, (void* base, size_t count, size_t element_size, crt_compar_fn compar), FROM, CRT_CLIENT_MODULE_NAME, "qsort");
    DECLARE(void*, crt_bsearch, (const void* key, const void* base, size_t count, size_t element_size, crt_compar_fn compar), FROM, CRT_CLIENT_MODULE_NAME, "bsearch");

    DECLARE(time_t, crt_time, (time_t* result), FROM, CRT_CLIENT_MODULE_NAME, "time");
    DECLARE(clock_t, crt_clock, (void), FROM, CRT_CLIENT_MODULE_NAME, "clock");
    DECLARE(tm*, crt_gmtime, (const time_t* value), FROM, CRT_CLIENT_MODULE_NAME, "gmtime");
    DECLARE(tm*, crt_localtime, (const time_t* value), FROM, CRT_CLIENT_MODULE_NAME, "localtime");
    DECLARE(time_t, crt_mktime, (tm* value), FROM, CRT_CLIENT_MODULE_NAME, "mktime");
    DECLARE(size_t, crt_strftime, (char* buffer, size_t size, const char* format, const tm* time_info), FROM, CRT_CLIENT_MODULE_NAME, "strftime");
    DECLARE(size_t, crt_mbstowcs, (wchar_t* destination, const char* source, size_t max_length), FROM, CRT_CLIENT_MODULE_NAME, "mbstowcs");
    DECLARE(size_t, crt_wcstombs, (char* destination, const wchar_t* source, size_t max_length), FROM, CRT_CLIENT_MODULE_NAME, "wcstombs");
    DECLARE(size_t, crt_wcslen, (const wchar_t* text), FROM, CRT_CLIENT_MODULE_NAME, "wcslen");
    DECLARE(int, crt_wcscmp, (const wchar_t* lhs, const wchar_t* rhs), FROM, CRT_CLIENT_MODULE_NAME, "wcscmp");
    DECLARE(int, crt_wcsncmp, (const wchar_t* lhs, const wchar_t* rhs, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "wcsncmp");
    DECLARE(wchar_t*, crt_wcscpy, (wchar_t* destination, const wchar_t* source), FROM, CRT_CLIENT_MODULE_NAME, "wcscpy");
    DECLARE(wchar_t*, crt_wcsncpy, (wchar_t* destination, const wchar_t* source, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "wcsncpy");
    DECLARE(wchar_t*, crt_wcschr, (const wchar_t* text, wchar_t value), FROM, CRT_CLIENT_MODULE_NAME, "wcschr");
    DECLARE(wchar_t*, crt_wcsrchr, (const wchar_t* text, wchar_t value), FROM, CRT_CLIENT_MODULE_NAME, "wcsrchr");
    DECLARE(void*, crt_wmemcpy, (void* destination, const void* source, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "wmemcpy");
    DECLARE(void*, crt_wmemset, (void* destination, wchar_t value, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "wmemset");
    DECLARE(int, crt_wmemcmp, (const void* lhs, const void* rhs, size_t size), FROM, CRT_CLIENT_MODULE_NAME, "wmemcmp");

    DECLARE(uintptr_t, __stack_chk_guard, (void), FROM, CRT_CLIENT_MODULE_NAME, "__stack_chk_guard");
    DECLARE(void, __stack_chk_fail, (void), FROM, CRT_CLIENT_MODULE_NAME, "__stack_chk_fail");
    DECLARE(int, __cxa_atexit, (void (*function)(void*), void* argument, void* dso_handle), FROM, CRT_CLIENT_MODULE_NAME, "__cxa_atexit");
    DECLARE(void, __cxa_finalize, (void* dso_handle), FROM, CRT_CLIENT_MODULE_NAME, "__cxa_finalize");
    extern void* __dso_handle;

#endif

#ifdef __cplusplus
}
#endif

#endif