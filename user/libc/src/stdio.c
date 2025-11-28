#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <ctype.h>

int putchar(int c)
{
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int getchar(void)
{
    char c;
    if (read(0, &c, 1) == 1)
        return (unsigned char)c;
    return EOF;
}

char *gets(char *s)
{
    char *p = s;
    int c;
    while ((c = getchar()) != EOF && c != '\n')
        *p++ = (char)c;
    *p = '\0';
    if (c == EOF && p == s)
        return NULL;
    return s;
}

struct out_ctx
{
    char *buf;
    size_t size;
    size_t pos;
    int count;
    bool is_buffer;
};

static void out_char(struct out_ctx *ctx, char c)
{
    if (ctx->is_buffer)
    {
        if (ctx->pos + 1 < ctx->size)
            ctx->buf[ctx->pos] = c;
    }
    else
    {
        putchar(c);
    }
    ctx->pos++;
    ctx->count++;
}

static int strnlen_s(const char *s, int limit)
{
    int len = 0;
    while (s && *s && len < limit)
    {
        s++;
        len++;
    }
    return len;
}

static void out_padding(struct out_ctx *ctx, int width, int content_len, char pad)
{
    int pads = width - content_len;
    if (pads < 0)
        pads = 0;
    for (int i = 0; i < pads; i++)
        out_char(ctx, pad);
}

static int format_uint(char *buf, size_t cap, unsigned long val, int base, bool lowercase)
{
    const char *digits = lowercase ? "0123456789abcdef" : "0123456789ABCDEF";
    int i = 0;
    if (val == 0 && cap > 1)
    {
        buf[i++] = '0';
    }
    else
    {
        while (val > 0 && i + 1 < (int)cap)
        {
            buf[i++] = digits[val % (unsigned)base];
            val /= (unsigned)base;
        }
    }
    buf[i] = '\0';
    // reverse
    for (int l = 0, r = i - 1; l < r; l++, r--)
    {
        char tmp = buf[l];
        buf[l] = buf[r];
        buf[r] = tmp;
    }
    return i;
}

static void format_double(char *buf, size_t cap, double val, int precision)
{
    if (cap == 0)
        return;
    char *p = buf;
    size_t remaining = cap;
    if (val < 0)
    {
        *p++ = '-';
        remaining--;
        val = -val;
    }
    unsigned long whole = (unsigned long)val;
    double frac_d = val - (double)whole;
    char intbuf[32];
    int intlen = format_uint(intbuf, sizeof intbuf, whole, 10, true);
    for (int i = 0; i < intlen && remaining > 1; i++)
    {
        *p++ = intbuf[i];
        remaining--;
    }
    if (precision > 0 && remaining > 1)
    {
        *p++ = '.';
        remaining--;
        double scale = 1.0;
        for (int i = 0; i < precision; i++)
            scale *= 10.0;
        unsigned long frac = (unsigned long)(frac_d * scale + 0.5);
        char fracbuf[32];
        int fraclen = format_uint(fracbuf, sizeof fracbuf, frac, 10, true);
        // pad leading zeros if needed
        while (fraclen < precision && remaining > 1)
        {
            *p++ = '0';
            remaining--;
            precision--;
        }
        for (int i = 0; i < fraclen && remaining > 1; i++)
        {
            *p++ = fracbuf[i];
            remaining--;
        }
    }
    *p = '\0';
}

static void vformat(struct out_ctx *ctx, const char *format, va_list args)
{
    for (const char *p = format; *p; p++)
    {
        if (*p != '%')
        {
            out_char(ctx, *p);
            continue;
        }
        p++;
        int width = 0;
        int precision = -1;
        bool long_mod = false;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            p++;
        }
        if (*p == '.')
        {
            precision = 0;
            p++;
            while (*p >= '0' && *p <= '9')
            {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }
        if (*p == 'l')
        {
            long_mod = true;
            p++;
        }

        switch (*p)
        {
        case 'd':
        case 'i':
        {
            long val = long_mod ? va_arg(args, long) : va_arg(args, int);
            char numbuf[32];
            int len = 0;
            unsigned long abs = (val < 0) ? (unsigned long)(-val) : (unsigned long)val;
            len = format_uint(numbuf, sizeof numbuf, abs, 10, true);
            int total_len = len + (val < 0 ? 1 : 0);
            out_padding(ctx, width, total_len, ' ');
            if (val < 0)
                out_char(ctx, '-');
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            break;
        }
        case 'u':
        {
            unsigned long val = long_mod ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            char numbuf[32];
            int len = format_uint(numbuf, sizeof numbuf, val, 10, true);
            out_padding(ctx, width, len, ' ');
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            break;
        }
        case 'x':
        {
            unsigned long val = long_mod ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            char numbuf[32];
            int len = format_uint(numbuf, sizeof numbuf, val, 16, true);
            out_padding(ctx, width, len, ' ');
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            break;
        }
        case 'p':
        {
            unsigned long val = va_arg(args, unsigned long);
            out_char(ctx, '0');
            out_char(ctx, 'x');
            char numbuf[32];
            int len = format_uint(numbuf, sizeof numbuf, val, 16, true);
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            break;
        }
        case 's':
        {
            char *s = va_arg(args, char *);
            if (!s)
                s = "(null)";
            int len = strnlen_s(s, 1024);
            if (precision >= 0 && precision < len)
                len = precision;
            out_padding(ctx, width, len, ' ');
            for (int i = 0; i < len; i++)
                out_char(ctx, s[i]);
            break;
        }
        case 'c':
        {
            int c = va_arg(args, int);
            out_char(ctx, (char)c);
            break;
        }
        case 'f':
        {
            if (precision < 0)
                precision = 6;
            double val = va_arg(args, double);
            char buf[64] = {0};
            format_double(buf, sizeof buf, val, precision);
            int len = strnlen_s(buf, 64);
            out_padding(ctx, width, len, ' ');
            for (int i = 0; i < len; i++)
                out_char(ctx, buf[i]);
            break;
        }
        case '%':
            out_char(ctx, '%');
            break;
        default:
            out_char(ctx, '%');
            if (*p)
                out_char(ctx, *p);
            else
                p--;
            break;
        }
    }
}

int vsnprintf(char *buf, size_t size, const char *format, va_list args)
{
    struct out_ctx ctx = {
        .buf = buf,
        .size = size,
        .pos = 0,
        .count = 0,
        .is_buffer = true,
    };
    vformat(&ctx, format, args);
    if (size > 0)
    {
        if (ctx.pos >= size)
            buf[size - 1] = '\0';
        else
            buf[ctx.pos] = '\0';
    }
    return ctx.count;
}

int snprintf(char *buf, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int res = vsnprintf(buf, size, format, args);
    va_end(args);
    return res;
}

int printf(const char *format, ...)
{
    struct out_ctx ctx = {
        .buf = NULL,
        .size = 0,
        .pos = 0,
        .count = 0,
        .is_buffer = false,
    };
    va_list args;
    va_start(args, format);
    vformat(&ctx, format, args);
    va_end(args);
    return ctx.count;
}

int puts(const char *s)
{
    while (*s)
        putchar(*s++);
    putchar('\n');
    return 0;
}

int sscanf(const char *str, const char *format, ...)
{
    if (!str || !format)
        return 0;

    va_list args;
    va_start(args, format);
    int assigned = 0;
    const char *s = str;

    for (const char *f = format; *f; f++)
    {
        if (isspace((unsigned char)*f))
        {
            while (isspace((unsigned char)*s))
                s++;
            continue;
        }

        if (*f != '%')
        {
            if (*s != *f)
                break;
            s++;
            continue;
        }

        f++;
        if (*f == '%')
        {
            if (*s != '%')
                break;
            s++;
            continue;
        }

        int base = 10;
        bool is_unsigned = false;
        switch (*f)
        {
        case 'd':
            base = 10;
            break;
        case 'i':
            base = 0; // auto-detect via strtol
            break;
        case 'x':
            base = 16;
            break;
        case 'o':
            base = 8;
            break;
        case 'u':
            base = 10;
            is_unsigned = true;
            break;
        default:
            goto out;
        }

        while (isspace((unsigned char)*s))
            s++;
        if (*s == '\0')
            break;

        char *endptr = NULL;
        long val = strtol(s, &endptr, base);
        if (endptr == s)
            break;

        if (is_unsigned)
        {
            unsigned int *out = va_arg(args, unsigned int *);
            if (out)
                *out = (unsigned int)val;
        }
        else
        {
            int *out = va_arg(args, int *);
            if (out)
                *out = (int)val;
        }

        s = endptr;
        assigned++;
    }

out:
    va_end(args);
    return assigned;
}
