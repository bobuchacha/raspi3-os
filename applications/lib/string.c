#include "string.h"

void *memcpy(void *dest, const void *src, size_t size)
{
    unsigned char *dest_bytes = (unsigned char *)dest;
    const unsigned char *src_bytes = (const unsigned char *)src;

    for (size_t index = 0; index < size; index++)
    {
        dest_bytes[index] = src_bytes[index];
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t size)
{
    unsigned char *dest_bytes = (unsigned char *)dest;
    const unsigned char *src_bytes = (const unsigned char *)src;

    if (dest_bytes == src_bytes || size == 0)
    {
        return dest;
    }

    if (dest_bytes < src_bytes)
    {
        return memcpy(dest, src, size);
    }

    for (size_t index = size; index > 0; index--)
    {
        dest_bytes[index - 1] = src_bytes[index - 1];
    }

    return dest;
}

void *memset(void *dest, int value, size_t size)
{
    unsigned char *dest_bytes = (unsigned char *)dest;

    for (size_t index = 0; index < size; index++)
    {
        dest_bytes[index] = (unsigned char)value;
    }

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t size)
{
    const unsigned char *lhs_bytes = (const unsigned char *)lhs;
    const unsigned char *rhs_bytes = (const unsigned char *)rhs;

    for (size_t index = 0; index < size; index++)
    {
        if (lhs_bytes[index] != rhs_bytes[index])
        {
            return (int)lhs_bytes[index] - (int)rhs_bytes[index];
        }
    }

    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0;

    while (text[length] != '\0')
    {
        length++;
    }

    return length;
}

size_t strnlen(const char *text, size_t max_size)
{
    size_t length = 0;

    while (length < max_size && text[length] != '\0')
    {
        length++;
    }

    return length;
}

int strcmp(const char *lhs, const char *rhs)
{
    while (*lhs != '\0' && *lhs == *rhs)
    {
        lhs++;
        rhs++;
    }

    return (unsigned char)*lhs - (unsigned char)*rhs;
}

int strncmp(const char *lhs, const char *rhs, size_t size)
{
    for (size_t index = 0; index < size; index++)
    {
        if (lhs[index] != rhs[index] || lhs[index] == '\0' || rhs[index] == '\0')
        {
            return (unsigned char)lhs[index] - (unsigned char)rhs[index];
        }
    }

    return 0;
}

char *strcpy(char *dest, const char *src)
{
    size_t index = 0;

    do
    {
        dest[index] = src[index];
    } while (src[index++] != '\0');

    return dest;
}

char *strncpy(char *dest, const char *src, size_t size)
{
    size_t index = 0;

    for (; index < size && src[index] != '\0'; index++)
    {
        dest[index] = src[index];
    }

    for (; index < size; index++)
    {
        dest[index] = '\0';
    }

    return dest;
}