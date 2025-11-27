#include "string.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include "kasan.h"

#ifdef KASAN
#define KASAN_ASSERT_RANGE(addr, size, is_write)                          \
    do                                                                    \
    {                                                                     \
        if (kasan_is_ready())                                             \
            kasan_check_range((addr), (size), (is_write), __builtin_return_address(0)); \
    } while (0)
#else
#define KASAN_ASSERT_RANGE(addr, size, is_write) \
    do                                           \
    {                                            \
        (void)(addr);                            \
        (void)(size);                            \
        (void)(is_write);                        \
    } while (0)
#endif

static long long read_signed_arg(va_list *args, int length_mod)
{
    if (length_mod >= 2)
        return va_arg(*args, long long);
    if (length_mod == 1)
        return va_arg(*args, long);
    return va_arg(*args, int);
}

static unsigned long long read_unsigned_arg(va_list *args, int length_mod)
{
    if (length_mod >= 2)
        return va_arg(*args, unsigned long long);
    if (length_mod == 1)
        return va_arg(*args, unsigned long);
    return va_arg(*args, unsigned int);
}

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
    KASAN_ASSERT_RANGE(dest, n, true);
    KASAN_ASSERT_RANGE(src, n, false);
    char *d = dest;
    const char *s = src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dest;
}


void *memmove(void *dst, const void *src, size_t n)
{
    const char *s = src;
    char *d       = dst;
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

void *memset(void *s, int c, size_t n)
{
    KASAN_ASSERT_RANGE(s, n, true);
    unsigned char *p = s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    KASAN_ASSERT_RANGE(s1, n, false);
    KASAN_ASSERT_RANGE(s2, n, false);
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
    const char *last = nullptr;
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

static int cb_emit_unsigned(uint64_t value, unsigned base, bool uppercase, void *arg, printf_callback_t callback)
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

    int written = idx ? idx : 1;
    while (idx-- > 0)
    {
        callback(tmp[idx], arg);
    }
    return written;
}

int vcbprintf(void *arg, printf_callback_t callback, const char *format, va_list *args)
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

        bool left_align = false;
        int width = 0;

        if (*format == '-')
        {
            left_align = true;
            format++;
        }

        while (*format >= '0' && *format <= '9')
        {
            width = width * 10 + (*format - '0');
            format++;
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

        char numbuf[64];
        int content_len = 0;

        switch (spec)
        {
        case 's':
        {
            const char *s = va_arg(*args, const char *);
            if (!s)
                s = "(null)";
            size_t len = strlen(s);
            content_len = (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
            int pad = (width > content_len) ? (width - content_len) : 0;
            if (!left_align)
                while (pad--) { callback(' ', arg); total++; }
            cb_emit_string(s, arg, callback);
            total += content_len;
            if (left_align)
                while (pad--) { callback(' ', arg); total++; }
            break;
        }
        case 'c':
        {
            char c = (char)va_arg(*args, int);
            content_len = 1;
            int pad = (width > content_len) ? (width - content_len) : 0;
            if (!left_align)
                while (pad--) { callback(' ', arg); total++; }
            callback(c, arg);
            total++;
            if (left_align)
                while (pad--) { callback(' ', arg); total++; }
            break;
        }
        case 'd':
        case 'i':
        {
            long long value = read_signed_arg(args, length_mod);

            bool negative = value < 0;
            unsigned long long magnitude = negative ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;

            int idx = 0;
            if (magnitude == 0)
                numbuf[idx++] = '0';
            while (magnitude && idx < (int)sizeof(numbuf))
            {
                numbuf[idx++] = (char)('0' + (magnitude % 10));
                magnitude /= 10;
            }
            if (negative && idx < (int)sizeof(numbuf))
                numbuf[idx++] = '-';

            content_len = idx;
            int pad = (width > content_len) ? (width - content_len) : 0;
            if (!left_align)
                while (pad--) { callback(' ', arg); total++; }
            while (idx-- > 0)
                callback(numbuf[idx], arg);
            total += content_len;
            if (left_align)
                while (pad--) { callback(' ', arg); total++; }
            break;
        }
        case 'u':
        {
            unsigned long long value = read_unsigned_arg(args, length_mod);

            int digits = cb_emit_unsigned(value, 10, false, arg, callback);
            content_len = digits;
            if (width > content_len && !left_align)
            {
                // Need to pad left; emit padding before digits
                int pad = width - content_len;
                // Move the digits output after padding:
                // Simpler: re-render with padding using buffer.
                // Build string in reverse then emit with padding.
                int idx = 0;
                unsigned long long tmp = value;
                if (tmp == 0)
                    numbuf[idx++] = '0';
                while (tmp && idx < (int)sizeof(numbuf))
                {
                    numbuf[idx++] = (char)('0' + (tmp % 10));
                    tmp /= 10;
                }
                // rewind the already emitted digits count
                // We can't "unwrite"; emit padding first then digits now.
                // Adjust total to include padding.
                // Emit padding
                for (int i = 0; i < pad; i++)
                {
                    callback(' ', arg);
                    total++;
                }
                // emit digits
                while (idx-- > 0)
                {
                    callback(numbuf[idx], arg);
                }
                total += content_len; // digits already counted
            }
            else if (width > content_len && left_align)
            {
                int pad = width - content_len;
                total += content_len;
                while (pad--) { callback(' ', arg); total++; }
            }
            else
            {
                total += content_len;
            }
            break;
        }
        case 'x':
        case 'X':
        case 'p':
        {
            bool uppercase = (spec == 'X');
            bool is_pointer = (spec == 'p');
            unsigned long long value = is_pointer ? (uintptr_t)va_arg(*args, void *)
                                                  : read_unsigned_arg(args, length_mod);

            int idx = 0;
            if (value == 0)
                numbuf[idx++] = '0';
            while (value && idx < (int)sizeof(numbuf))
            {
                unsigned digit = (unsigned)(value % 16);
                numbuf[idx++] = uppercase ? "0123456789ABCDEF"[digit] : "0123456789abcdef"[digit];
                value /= 16;
            }

            int prefix_len = is_pointer ? 2 : 0;
            content_len = prefix_len + idx;
            int pad = (width > content_len) ? (width - content_len) : 0;

            if (!left_align)
            {
                while (pad-- > 0) { callback(' ', arg); total++; }
            }

            if (is_pointer)
            {
                callback('0', arg);
                callback('x', arg);
            }

            while (idx-- > 0)
                callback(numbuf[idx], arg);

            if (left_align)
            {
                while (pad-- > 0) { callback(' ', arg); total++; }
            }

            total += content_len;
            break;
        }
        case 'o':
        {
            unsigned long long value = read_unsigned_arg(args, length_mod);
            int digits = cb_emit_unsigned(value, 8, false, arg, callback);
            content_len = digits;
            int pad = (width > content_len) ? (width - content_len) : 0;
            if (!left_align)
            {
                for (int i = 0; i < pad; i++) { callback(' ', arg); total++; }
                // digits already emitted; nothing to rewind cleanly, so re-emit
                int idx = 0;
                unsigned long long tmp = value;
                if (tmp == 0)
                    numbuf[idx++] = '0';
                while (tmp && idx < (int)sizeof(numbuf))
                {
                    numbuf[idx++] = (char)('0' + (tmp % 8));
                    tmp /= 8;
                }
                while (idx-- > 0)
                    callback(numbuf[idx], arg);
            }
            else
            {
                total += content_len;
                while (pad--) { callback(' ', arg); total++; }
                break;
            }
            total += content_len;
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

    va_list args_copy;
    va_copy(args_copy, args); // NOLINT(clang-analyzer-security.VAList)
    vcbprintf(&cctx, count_callback, format, &args_copy);
    va_end(args_copy);

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
