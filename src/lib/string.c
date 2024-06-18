#include <ros.h>
void *memset(void *s, int c, int n) {
    unsigned char *dst = s;

    /* Fill n bytes of s with c */
    while (n > 0) {
        *dst = (unsigned char) c;
        dst++;
        n--;
    }
    return s;
}

// void *memcpy(void *dest, const void *src, int n) {
//     /* Typecast src and dest addresses to correct data types */
//     char       *d = (char *) dest;
//     const char *s = (const char *) src;

//     /* Copy contents of *src, to *dest */
//     while (n > 0) {
//         *(d++) = *(s++);
//         n--;
//     }
//     return dest;
// }

int strlen(const char *s) {
    const char *char_ptr;

    /* Iterate string until null terminator */
    for (char_ptr = s; *char_ptr; ++char_ptr);
    /* Return the difference of (end - start) address -> length of string */
    return (int) (char_ptr - s);
}

int strcmp(const char *s1, const char *s2) {
    while ((*s1 != '\0') && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    /* Return the ASCII difference after convert. char* to unsigned char* */
    return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}
int strncmp(const char *s1, const char *s2, int len) {
    while ((--len > 0) && (*(s1++) == *(s2++))) {
        // s1++;
        // s2++;
    }

    /* Return the ASCII difference after convert. char* to unsigned char* */
    return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}

char *strcpy(char *dest, const char *src) {
    /* Return if no memory is allocated to the destination */
    if (dest == 0)
        return 0;

    char *tmp = dest;

    /* Copy the string pointed to by src, in dest */
    while (*src != '\0') {
        *(tmp++) = *(src++);
    }
    /* Include null terminator */
    *tmp      = '\0';

    /* Return starting address of dest */
    return dest;

}

char *strncpy(char *dest, const char *src, int length) {
    /* Return if no memory is allocated to the destination */
    if (dest == 0)
        return 0;

    char *tmp = dest;

    /* Copy the string pointed to by src, in dest */
    do {
        if ((*dest++ = *src++) == 0) {
            /* NUL pad the remaining n-1 bytes */
            while (--length != 0)
                *dest++ = 0;
            break;
        }
    } while (--length != 0);

    /* Return starting address of dest */
    return dest;

}

char *strcat(char *dest, const char *src) {
    char *tmp = dest;

    /* Move dest pointer to the end of string */
    while (*tmp != '\0')
        tmp++;

    /* Appends characters of source to the destination string */
    while (*src != '\0') {
        *(tmp++) = *(src++);
    }
    /* Include null terminator */
    *tmp      = '\0';

    /* Return starting address of dest */
    return dest;
}

void strrev(char *s) {
    int  i, j;
    char tmp;

    /* Reverse s string, char by char, using temp char */
    for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
        tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
    }
}

void bzero(void *b, int length) {
    memset((void *) b, 0, length);
}

int k_toupper(int c) {
    if (c >= 97 && c <= 122) {
        return c - 32;
    }
    return c;
}

int k_tolower(int c) {
    if (c >= 65 && c <= 90) {
        return c + 32;
    }
    return c;
}
char * itos(unsigned int myint, char buffer[], int bufflen)
{
    int i = bufflen - 2;
    buffer[bufflen-1] = 0;

    if(myint == 0) {
        buffer[i--] = '0';
    }

    while(myint > 0 && i >= 0)
    {
        buffer[i--] = (myint % 10) + '0';
        myint/=10;
    }

    return &buffer[i+1];
}
int coerce_int(char *s, unsigned int *val) {
    unsigned int result = 0;

    while (*s && *s != '\n') {
        if (*s >= 48 && *s <= 57) {
            result *= 10;
            result = result + (*s - 48);
        }
        else {
            return 0;
        }
        s++;
    }
    *val = result;
    return 1;
}

unsigned char hex_char(unsigned char byte) {
    byte = byte & 0x0F;
    if (byte < 0xA) {
        char buff[2];
        itos(byte, buff, 2);
        return buff[0];
    }
    else {
        switch (byte) {
            case 0x0A:
                return 'A';
                break;
            case 0x0B:
                return 'B';
                break;
            case 0x0C:
                return 'C';
                break;
            case 0x0D:
                return 'D';
                break;
            case 0x0E:
                return 'E';
                break;
            case 0x0F:
                return 'F';
                break;
        }
    }
    return 0;
}

char *
strtok(char *s, char * delim)
{
    register char *spanp;
    register int c, sc;
    char *tok;
    static char *last;


    if (s == NULL && (s = last) == NULL)
        return (NULL);

    /*
     * Skip (span) leading delimiters (s += strspn(s, delim), sort of).
     */
    cont:
    c = *s++;
    for (spanp = (char *)delim; (sc = *spanp++) != 0;) {
        if (c == sc)
            goto cont;
    }

    if (c == 0) {		/* no non-delimiter characters */
        last = NULL;
        return (NULL);
    }
    tok = s - 1;

    /*
     * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
     * Note that delim must have one NUL; we stop if we see that, too.
     */
    for (;;) {
        c = *s++;
        spanp = (char *)delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                last = s;
                return (tok);
            }
        } while (sc != 0);
    }
    /* NOTREACHED */
}