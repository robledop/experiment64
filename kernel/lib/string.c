#include "string.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
    {
        return 0;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *d = dest;
    const char *s = src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--)
    {
        if (*p1 != *p2)
        {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++)
    {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *ptr = dest + strlen(dest);
    while (*src != '\0')
    {
        *ptr++ = *src++;
    }
    *ptr = '\0';
    return dest;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    do
    {
        if (*s == (char)c)
            last = s;
    } while (*s++);
    return (char *)last;
}

static void buffer_emit_char(char c, char *buffer, size_t capacity, size_t *stored, int *total)
{
    if (buffer && *stored < capacity)
    {
        buffer[*stored] = c;
        (*stored)++;
    }
    (*total)++;
}

static void buffer_emit_string(const char *s, char *buffer, size_t capacity, size_t *stored, int *total)
{
    if (!s)
        s = "(null)";
    while (*s)
    {
        buffer_emit_char(*s++, buffer, capacity, stored, total);
    }
}

static void buffer_emit_unsigned(uint64_t value, unsigned base, bool uppercase, char *buffer, size_t capacity, size_t *stored, int *total)
{
    char tmp[32];
    int idx = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0)
    {
        tmp[idx++] = '0';
    }
    else
    {
        while (value && idx < (int)sizeof(tmp))
        {
            tmp[idx++] = digits[value % base];
            value /= base;
        }
    }

    while (idx-- > 0)
    {
        buffer_emit_char(tmp[idx], buffer, capacity, stored, total);
    }
}

int vsnprintk(char *buffer, size_t size, const char *format, va_list args)
{
    size_t capacity = (size > 0) ? size - 1 : 0;
    size_t stored = 0;
    int total = 0;

    while (*format)
    {
        if (*format != '%')
        {
            buffer_emit_char(*format++, buffer, capacity, &stored, &total);
            continue;
        }

        format++;
        if (*format == '%')
        {
            buffer_emit_char('%', buffer, capacity, &stored, &total);
            format++;
            continue;
        }

        int length_mod = 0;
        while (*format == 'l')
        {
            length_mod++;
            format++;
        }

        if (*format == '\0')
            break;

        char spec = *format++;

        switch (spec)
        {
        case 's':
            buffer_emit_string(va_arg(args, const char *), buffer, capacity, &stored, &total);
            break;
        case 'c':
        {
            char c = (char)va_arg(args, int);
            buffer_emit_char(c, buffer, capacity, &stored, &total);
            break;
        }
        case 'd':
        case 'i':
        {
            long long value;
            if (length_mod >= 2)
                value = va_arg(args, long long);
            else if (length_mod == 1)
                value = va_arg(args, long);
            else
                value = va_arg(args, int);

            unsigned long long magnitude;
            if (value < 0)
            {
                buffer_emit_char('-', buffer, capacity, &stored, &total);
                magnitude = (unsigned long long)(-(value + 1)) + 1;
            }
            else
            {
                magnitude = (unsigned long long)value;
            }

            buffer_emit_unsigned(magnitude, 10, false, buffer, capacity, &stored, &total);
            break;
        }
        case 'u':
        {
            unsigned long long value;
            if (length_mod >= 2)
                value = va_arg(args, unsigned long long);
            else if (length_mod == 1)
                value = va_arg(args, unsigned long);
            else
                value = va_arg(args, unsigned int);

            buffer_emit_unsigned(value, 10, false, buffer, capacity, &stored, &total);
            break;
        }
        case 'x':
        case 'X':
        case 'p':
        {
            bool uppercase = (spec == 'X');
            unsigned long long value;
            if (spec == 'p')
            {
                value = (uintptr_t)va_arg(args, void *);
                buffer_emit_char('0', buffer, capacity, &stored, &total);
                buffer_emit_char('x', buffer, capacity, &stored, &total);
            }
            else if (length_mod >= 2)
                value = va_arg(args, unsigned long long);
            else if (length_mod == 1)
                value = va_arg(args, unsigned long);
            else
                value = va_arg(args, unsigned int);

            buffer_emit_unsigned(value, 16, uppercase, buffer, capacity, &stored, &total);
            break;
        }
        case 'o':
        {
            unsigned long long value;
            if (length_mod >= 2)
                value = va_arg(args, unsigned long long);
            else if (length_mod == 1)
                value = va_arg(args, unsigned long);
            else
                value = va_arg(args, unsigned int);
            buffer_emit_unsigned(value, 8, false, buffer, capacity, &stored, &total);
            break;
        }
        default:
            buffer_emit_char('%', buffer, capacity, &stored, &total);
            buffer_emit_char(spec, buffer, capacity, &stored, &total);
            break;
        }
    }

    if (size > 0 && buffer)
    {
        buffer[stored] = '\0';
    }

    return total;
}

int snprintk(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintk(buffer, size, format, args);
    va_end(args);
    return ret;
}
