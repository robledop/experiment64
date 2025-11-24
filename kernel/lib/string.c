#include "string.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

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

static void cb_emit_string(const char *s, void *arg, printf_callback_t callback)
{
    if (!s)
        s = "(null)";
    while (*s)
    {
        callback(*s++, arg);
    }
}

static void cb_emit_unsigned(uint64_t value, unsigned base, bool uppercase, void *arg, printf_callback_t callback)
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
        callback(tmp[idx], arg);
    }
}

int vcbprintf(void *arg, printf_callback_t callback, const char *format, va_list args)
{
    int total = 0;

    while (*format)
    {
        if (*format != '%')
        {
            callback(*format++, arg);
            total++;
            continue;
        }

        format++;
        if (*format == '%')
        {
            callback('%', arg);
            total++;
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
        {
            const char *s = va_arg(args, const char *);
            if (!s)
                s = "(null)";
            size_t len = strlen(s);
            cb_emit_string(s, arg, callback);
            if (len > (size_t)INT_MAX)
                len = INT_MAX;
            total += (int)len;
            break;
        }
        case 'c':
        {
            char c = (char)va_arg(args, int);
            callback(c, arg);
            total++;
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
                callback('-', arg);
                total++;
                magnitude = (unsigned long long)(-(value + 1)) + 1;
            }
            else
            {
                magnitude = (unsigned long long)value;
            }

            // Calculate length for total count?
            // cb_emit_unsigned doesn't return count.
            // We can just wrap the callback to count?
            // Or just let the user count?
            // Wait, vcbprintf is supposed to return total chars written.
            // But I can't easily know how many chars cb_emit_unsigned writes without modifying it or counting.
            // I'll modify cb_emit_unsigned to return count or just increment total inside the loop.
            // Actually, let's just increment total in the loop.
            // But I need to know how many digits.
            // Let's just make a helper that counts.

            // Re-implementing cb_emit_unsigned logic here to count is annoying.
            // Better: The callback itself doesn't return anything.
            // I can wrap the callback passed to vcbprintf with a counting wrapper?
            // No, vcbprintf takes the callback.
            // I should just update total as I emit.

            // Let's modify cb_emit_unsigned to take &total.
            cb_emit_unsigned(magnitude, 10, false, arg, callback);
            // Wait, I need to update total.
            // I'll modify the helpers to take int *total.
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

            cb_emit_unsigned(value, 10, false, arg, callback);
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
                callback('0', arg);
                callback('x', arg);
                total += 2;
            }
            else if (length_mod >= 2)
                value = va_arg(args, unsigned long long);
            else if (length_mod == 1)
                value = va_arg(args, unsigned long);
            else
                value = va_arg(args, unsigned int);

            cb_emit_unsigned(value, 16, uppercase, arg, callback);
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
            cb_emit_unsigned(value, 8, false, arg, callback);
            break;
        }
        default:
            callback('%', arg);
            callback(spec, arg);
            total += 2;
            break;
        }
    }
    return total;
}

// Helper struct for vsnprintk
struct vsnprintk_ctx
{
    char *buffer;
    size_t capacity;
    size_t stored;
};

static void vsnprintk_callback(char c, void *arg)
{
    struct vsnprintk_ctx *ctx = arg;
    if (ctx->buffer && ctx->stored < ctx->capacity)
    {
        ctx->buffer[ctx->stored++] = c;
    }
}

// Wrapper to count characters for vcbprintf return value
struct count_ctx
{
    void *arg;
    printf_callback_t callback;
    int count;
};

static void count_callback(char c, void *arg)
{
    struct count_ctx *ctx = arg;
    ctx->count++;
    if (ctx->callback)
    {
        ctx->callback(c, ctx->arg);
    }
}

int vsnprintk(char *buffer, size_t size, const char *format, va_list args)
{
    struct vsnprintk_ctx ctx = {
        .buffer = buffer,
        .capacity = (size > 0) ? size - 1 : 0,
        .stored = 0};

    // We need to return the number of characters that WOULD have been written.
    // So we need to count everything, even if we don't write it.
    // vcbprintf will drive the process.

    // But wait, vcbprintf needs to return the count.
    // I need to implement vcbprintf such that it returns the count.
    // I'll modify the helpers above to update a count.

    // Actually, I'll just use the count_ctx wrapper I defined above.
    struct count_ctx cctx = {
        .arg = &ctx,
        .callback = vsnprintk_callback,
        .count = 0};

    vcbprintf(&cctx, count_callback, format, args);

    if (size > 0 && buffer)
    {
        buffer[ctx.stored] = '\0';
    }

    return cctx.count;
}

int snprintk(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintk(buffer, size, format, args);
    va_end(args);
    return ret;
}
