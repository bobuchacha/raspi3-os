/*
File: printf.c

Copyright (C) 2004  Kustaa Nyholm

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "printf.h"

typedef void (*putcf)(void*, char);
static putcf stdout_putf;
static void* stdout_putp;

#define PRINTF_LONG_SUPPORT

#ifdef PRINTF_LONG_SUPPORT

static void uli2a(unsigned long long num, unsigned long base, int uc, char* bf) {
    register int n = 0;
    register unsigned long int d = 1;
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
    uli2a(num, 10, 0, bf);
}

#endif

static void ui2a(unsigned int num, unsigned int base, int uc, char* bf) {

    int n = 0;
    unsigned int d = 1;
    while (num / d >= base)
        d *= base;
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
    ui2a(num, 10, 0, bf);
}

static int a2d(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    else
        return -1;
}

static char a2i(char ch, char** src, int base, int* nump) {
    char* p = *src;
    int num = 0;
    int digit;
    while ((digit = a2d(ch)) >= 0) {
        if (digit > base)
            break;
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
    while (*p++ && n > 0)
        n--;
    while (n-- > 0)
        putf(putp, fc);
    while ((ch = *bf++))
        putf(putp, ch);
}

void tfp_format(void* putp, putcf putf, char* fmt, va_list va) {
    char bf[12];

    char ch;

    while ((ch = *(fmt++))) {
        if (ch != '%')
            putf(putp, ch);
        else {
            char lz = 0;
#ifdef PRINTF_LONG_SUPPORT
            char lng = 0;
#endif
            int w = 0;
            ch = *(fmt++);
            if (ch == '0') {
                ch = *(fmt++);
                lz = 1;
            }
            if (ch >= '0' && ch <= '9') {
                ch = a2i(ch, &fmt, 10, &w);
            }
#ifdef PRINTF_LONG_SUPPORT
            if (ch == 'l') {
                ch = *(fmt++);
                lng = 1;
            }
#endif
            switch (ch) {
            case 0:
                goto abort;
            case 'u':
            {
#ifdef PRINTF_LONG_SUPPORT
                if (lng)
                    uli2a(va_arg(va, unsigned long int), 10, 0, bf);
                else
#endif
                    ui2a(va_arg(va, unsigned int), 10, 0, bf);
                putchw(putp, putf, w, lz, bf);
                break;
            }
            case 'd':
            {
#ifdef PRINTF_LONG_SUPPORT
                if (lng)
                    li2a(va_arg(va, unsigned long int), bf);
                else
#endif
                    i2a(va_arg(va, int), bf);
                putchw(putp, putf, w, lz, bf);
                break;
            }
            case 'x':
            case 'X':
#ifdef PRINTF_LONG_SUPPORT
                if (lng)
                    uli2a(va_arg(va, unsigned long int), 16, (ch == 'X'), bf);
                else
#endif
                    ui2a(va_arg(va, unsigned int), 16, (ch == 'X'), bf);
                putchw(putp, putf, w, lz, bf);
                break;
            case 'c':
                putf(putp, (char)(va_arg(va, int)));
                break;
            case 's':
                putchw(putp, putf, w, 0, va_arg(va, char*));
                break;
            case '%':
                putf(putp, ch);
            default:
                break;
            }
        }
    }
abort:;
}

void init_printf(void(*putp), void (*putf)(void*, char)) {
    stdout_putf = putf;
    stdout_putp = putp;
    kinfo("init_printf: printf ready");
}

void tfp_printf(char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    va_end(va);
}

// Serialize console writes across CPUs so log records stay readable.
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
    asm volatile(
        "stlr %w1, [%0]\n"
        :
    : "r"(lock), "r"(0U)
        : "memory");
}

/**
 * console_lock
 *
 * Acquire the console spinlock so a multi-call log record can write to the
 * UART without being interleaved by another CPU.
 */
void console_lock(void) {
    while (!spinlock_try_acquire(&console_spinlock)) {
        asm volatile("yield\n"); // back off while another CPU owns the UART
    }
}

/**
 * console_unlock
 *
 * Release the console spinlock after a serialized log record is finished.
 */
void console_unlock(void) {
    spinlock_release(&console_spinlock);
}

// prototype of printf
void kprint(char* fmt, ...) {
    va_list va;
    // console_lock();      // this create hang when kprint is called before the scheduler starts, so we will rely on the caller to lock the console when needed
    va_start(va, fmt);
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    va_end(va);
    // console_unlock();
}

void kdebug(char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf("\x1b[34m[DEBUG]\x1b[0m     ");
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    printf("\n");
    va_end(va);
}
void kinfo(char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf("\x1b[32m[INFO]\x1b[0m      ");
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    printf("\n");
    va_end(va);
}
void kerror(char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf("\x1b[31m[ERROR]\x1b[0m     ");
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    printf("\n");
    va_end(va);
}
void kpanic(char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf("\x1b[31;1m=================================== [PANIC] ===================================\x1b[0m\n");
    tfp_format(stdout_putp, stdout_putf, fmt, va);
    printf("\n");
    va_end(va);
    asm volatile("brk #0xFF"); // call debugger
}

void print_c(unsigned char c) {
    stdout_putf(stdout_putp, c);
}

/**
 * Dump memory
 */
void kdump(void* ptr) {
    kdump_size(ptr, 512);
}

void kdump_region(void* ptr, int size, unsigned long display_base) {
    unsigned long a;
    unsigned long b;
    unsigned long d;
    unsigned char c;
    unsigned long start = (unsigned long int)ptr;

    printf("\n\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    printf("\x1b[1;36mHex Dump\x1b[0m \x1b[2;37m|\x1b[0m \x1b[36m0x%lX\x1b[0m \x1b[2;37m+\x1b[0m \x1b[97m%d bytes\x1b[0m\n", display_base, size);
    printf("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
    for (a = start; a < start + size; a += 16) {
        printf("\x1b[1;34m%016lX\x1b[0m  ", display_base + (a - start));
        for (b = 0; b < 16; b++) {
            c = *((unsigned char*)(a + b));
            d = (unsigned int)c;
            d >>= 4;
            d &= 0xF;
            d += d > 9 ? 0x37 : 0x30;
            print_c(d);
            d = (unsigned int)c;
            d &= 0xF;
            d += d > 9 ? 0x37 : 0x30;
            print_c(d);
            print_c(' ');
            if (b % 4 == 3)
                print_c(' ');
        }
        print_c(' ');
        print_c('|');
        print_c(' ');
        for (b = 0; b < 16; b++) {
            c = *((unsigned char*)(a + b));
            print_c(c < 32 || c >= 127 ? '.' : c);
        }
        print_c('\r');
        print_c('\n');
    }
    printf("\x1b[2;37m-------------------------------------------------------------------------------\x1b[0m\n");
}

void kdump_size(void* ptr, int size) {
    kdump_region(ptr, size, (unsigned long)ptr);
}

static void putcp(void* p, char c) {
    *(*((char**)p))++ = c;
}

int tfp_sprintf(char* s, char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    tfp_format(&s, putcp, fmt, va);
    putcp(&s, 0);
    va_end(va);
    return 0;
}
