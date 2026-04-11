#include "printf.h"

typedef void (*putcf)(void*, char);
static putcf stdout_putf;
static void* stdout_putp;

#define PRINTF_LONG_SUPPORT

static void ui2a(unsigned int num, unsigned int base, int uc, char* bf) {
    int n = 0;
    unsigned int d = 1;

    while (num / d >= base) {
        d *= base;
    }
    while (d != 0) {
        int dgt = num / d;
        num %= d;
        d /= base;
        if (n || dgt > 0 || d == 0) {
            *bf++ = dgt + (dgt < 10 ? '0' : (uc ? 'A' : 'a') - 10);
            ++n;
        }
    }
    *bf = 0;
}

static void i2a(int num, char* bf) {
    if (num < 0) {
        num = -num;
        *bf++ = '-';
    }
    ui2a((unsigned int)num, 10, 0, bf);
}

static void uli2a(unsigned long long num, unsigned long base, int uc, char* bf) {
    int n = 0;
    unsigned long d = 1;

    while (num / d >= base) {
        d *= base;
    }
    while (d != 0) {
        int dgt = num / d;
        num %= d;
        d /= base;
        if (n || dgt > 0 || d == 0) {
            *bf++ = dgt + (dgt < 10 ? '0' : (uc ? 'A' : 'a') - 10);
            ++n;
        }
    }
    *bf = 0;
}

static void li2a(long num, char* bf) {
    if (num < 0) {
        num = -num;
        *bf++ = '-';
    }
    uli2a((unsigned long)num, 10, 0, bf);
}

static int a2d(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static char a2i(char ch, const char** src, int base, int* nump) {
    const char* p = *src;
    int num = 0;
    int digit;

    while ((digit = a2d(ch)) >= 0) {
        if (digit > base) {
            break;
        }
        num = num * base + digit;
        ch = *p++;
    }
    *src = p;
    *nump = num;
    return ch;
}

static void putchw(void* putp, putcf putf, int n, char z, char* bf) {
    char fc = z ? '0' : ' ';
    char ch;
    char* p = bf;

    while (*p++ && n > 0) {
        n--;
    }
    while (n-- > 0) {
        putf(putp, fc);
    }
    while ((ch = *bf++)) {
        putf(putp, ch);
    }
}

void tfp_format(void* putp, void (*putf)(void*, char), const char* fmt, va_list va) {
    char bf[24];
    char ch;

    while ((ch = *(fmt++))) {
        if (ch != '%') {
            putf(putp, ch);
        } else {
            char lz = 0;
            char lng = 0;
            int w = 0;

            ch = *(fmt++);
            if (ch == '0') {
                ch = *(fmt++);
                lz = 1;
            }
            if (ch >= '0' && ch <= '9') {
                ch = a2i(ch, &fmt, 10, &w);
            }
            if (ch == 'l') {
                ch = *(fmt++);
                lng = 1;
            }
            switch (ch) {
            case 0:
                return;
            case 'u':
                if (lng) {
                    uli2a(va_arg(va, unsigned long), 10, 0, bf);
                } else {
                    ui2a(va_arg(va, unsigned int), 10, 0, bf);
                }
                putchw(putp, putf, w, lz, bf);
                break;
            case 'd':
                if (lng) {
                    li2a(va_arg(va, long), bf);
                } else {
                    i2a(va_arg(va, int), bf);
                }
                putchw(putp, putf, w, lz, bf);
                break;
            case 'x':
            case 'X':
                if (lng) {
                    uli2a(va_arg(va, unsigned long), 16, (ch == 'X'), bf);
                } else {
                    ui2a(va_arg(va, unsigned int), 16, (ch == 'X'), bf);
                }
                putchw(putp, putf, w, lz, bf);
                break;
            case 'c':
                putf(putp, (char)va_arg(va, int));
                break;
            case 's':
                putchw(putp, putf, w, 0, va_arg(va, char*));
                break;
            case '%':
                putf(putp, ch);
                break;
            default:
                break;
            }
        }
    }
}

void init_printf(void* putp, void (*putf)(void*, char)) {
    stdout_putf = putf;
    stdout_putp = putp;
}

void tfp_printf(const char* fmt, ...) {
    va_list va;

    va_start(va, fmt);
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    va_end(va);
}

static volatile unsigned int console_spinlock = 0;

static inline unsigned int spinlock_try_acquire(volatile unsigned int* lock) {
    unsigned int previous;
    unsigned int status;

    asm volatile(
        "ldaxr %w0, [%2]\n"
        "cbnz %w0, 1f\n"
        "mov %w0, #1\n"
        "stxr %w1, %w0, [%2]\n"
        "cbnz %w1, 2f\n"
        "mov %w0, wzr\n"
        "b 3f\n"
        "1:\n"
        "mov %w1, wzr\n"
        "2:\n"
        "3:\n"
        : "=&r"(previous), "=&r"(status)
        : "r"(lock)
        : "memory");

    return previous == 0U && status == 0U;
}

static inline void spinlock_release(volatile unsigned int* lock) {
    asm volatile("stlr %w1, [%0]\n" : : "r"(lock), "r"(0U) : "memory");
}

void console_lock(void) {
    while (!spinlock_try_acquire(&console_spinlock)) {
        asm volatile("yield\n");
    }
}

void console_unlock(void) {
    spinlock_release(&console_spinlock);
}

void kprint(const char* fmt, ...) {
    va_list va;

    va_start(va, fmt);
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    va_end(va);
}

void kerror(const char* fmt, ...) {
    va_list va;

    va_start(va, fmt);
    console_lock();
    kprint("[error] ");
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    kprint("\r\n");
    console_unlock();
    va_end(va);
}

void kpanic(const char* fmt, ...) {
    va_list va;

    va_start(va, fmt);
    console_lock();
    kprint("[panic] ");
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    kprint("\r\n");
    console_unlock();
    va_end(va);
    while (1) {
    }
}

static void putcp(void* p, char c) {
    *(*((char**)p))++ = c;
}

int tfp_sprintf(char* s, const char* fmt, ...) {
    va_list va;

    va_start(va, fmt);
    tfp_format(&s, putcp, fmt, va);
    putcp(&s, 0);
    va_end(va);
    return 0;
}
