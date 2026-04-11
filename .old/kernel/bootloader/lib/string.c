#include "ros.h"

void* memset(void* s, int c, int n) {
    unsigned char* dst = (unsigned char*)s;

    while (n-- > 0) {
        *dst++ = (unsigned char)c;
    }
    return s;
}

void* memmove(void* dst, const void* src, unsigned int n) {
    const unsigned char* s = (const unsigned char*)src;
    unsigned char* d = (unsigned char*)dst;

    if (s < d && s + n > d) {
        s += n;
        d += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    } else {
        while (n-- > 0) {
            *d++ = *s++;
        }
    }

    return dst;
}

int memcmp(const void* lhs, const void* rhs, unsigned int n) {
    const unsigned char* left = (const unsigned char*)lhs;
    const unsigned char* right = (const unsigned char*)rhs;

    while (n-- > 0) {
        if (*left != *right) {
            return *left - *right;
        }
        left++;
        right++;
    }

    return 0;
}

int strlen(const char* s) {
    const char* end = s;

    while (*end) {
        end++;
    }

    return (int)(end - s);
}

int strcmp(const char* s1, const char* s2) {
    while ((*s1 != '\0') && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int length) {
    if (length == 0) {
        return 0;
    }

    do {
        if (*s1 != *s2++) {
            return *(const unsigned char*)s1 - *(const unsigned char*)--s2;
        }
        if (*s1++ == 0) {
            break;
        }
    } while (--length != 0);

    return 0;
}

char* strncpy(char* dest, const char* src, int length) {
    char* out = dest;

    if (dest == 0) {
        return 0;
    }

    while (length-- > 0) {
        if ((*dest++ = *src++) == 0) {
            while (length-- > 0) {
                *dest++ = 0;
            }
            break;
        }
    }

    return out;
}

int k_toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}
