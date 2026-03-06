#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
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
        return nullptr;
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

static int format_uint(char *buf, size_t cap, unsigned long long val, int base, bool lowercase)
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
    unsigned long long whole = (unsigned long long)val;
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
        unsigned long long frac = (unsigned long long)(frac_d * scale + 0.5);
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
        bool left_align = false;
        bool zero_pad = false;
        int width = 0;
        int precision = -1;
        enum
        {
            LEN_NONE,
            LEN_LONG,
            LEN_LLONG,
            LEN_SIZE
        } len_mod = LEN_NONE;
        if (*p == '-')
        {
            left_align = true;
            p++;
        }
        if (*p == '0')
        {
            zero_pad = true;
            p++;
        }
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
            if (*(p + 1) == 'l')
            {
                len_mod = LEN_LLONG;
                p += 2;
            }
            else
            {
                len_mod = LEN_LONG;
                p++;
            }
        }
        else if (*p == 'z')
        {
            len_mod = LEN_SIZE;
            p++;
        }
        if (len_mod == LEN_SIZE)
            len_mod = LEN_LONG;

        switch (*p)
        {
        case 'd':
        case 'i':
        {
            long long val;
            switch (len_mod)
            {
            case LEN_LLONG:
            {
                long long tmp = va_arg(args, long long);
                val = tmp;
                break;
            }
            case LEN_LONG:
            {
                long tmp = va_arg(args, long);
                val = tmp;
                break;
            }
            default:
            {
                int tmp = va_arg(args, int);
                val = tmp;
                break;
            }
            }
            char numbuf[32];
            int len = 0;
            unsigned long long abs = (val < 0) ? (unsigned long long)(-(val + 1)) + 1
                                               : (unsigned long long)val;
            len = format_uint(numbuf, sizeof numbuf, abs, 10, true);
            int pad_zero = (precision > len) ? (precision - len) : 0;
            int total_len = len + pad_zero + (val < 0 ? 1 : 0);
            if (zero_pad && val < 0)
                out_char(ctx, '-');
            if (!left_align)
                out_padding(ctx, width, total_len, zero_pad ? '0' : ' ');
            if (!zero_pad && val < 0)
                out_char(ctx, '-');
            while (pad_zero-- > 0)
                out_char(ctx, '0');
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            if (left_align)
                out_padding(ctx, width, total_len, ' ');
            break;
        }
        case 'u':
        {
            unsigned long long val;
            switch (len_mod)
            {
            case LEN_LLONG:
            {
                unsigned long long tmp = va_arg(args, unsigned long long);
                val = tmp;
                break;
            }
            case LEN_LONG:
            {
                unsigned long tmp = va_arg(args, unsigned long);
                val = tmp;
                break;
            }
            default:
            {
                unsigned int tmp = va_arg(args, unsigned int);
                val = tmp;
                break;
            }
            }
            char numbuf[32];
            int len = format_uint(numbuf, sizeof numbuf, val, 10, true);
            int pad_zero = (precision > len) ? (precision - len) : 0;
            if (!left_align)
                out_padding(ctx, width, len + pad_zero, zero_pad ? '0' : ' ');
            while (pad_zero-- > 0)
                out_char(ctx, '0');
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            if (left_align)
                out_padding(ctx, width, len + pad_zero, ' ');
            break;
        }
        case 'x':
        {
            unsigned long long val;
            switch (len_mod)
            {
            case LEN_LLONG:
            {
                unsigned long long tmp = va_arg(args, unsigned long long);
                val = tmp;
                break;
            }
            case LEN_LONG:
            {
                unsigned long tmp = va_arg(args, unsigned long);
                val = tmp;
                break;
            }
            default:
            {
                unsigned int tmp = va_arg(args, unsigned int);
                val = tmp;
                break;
            }
            }
            char numbuf[32];
            int len = format_uint(numbuf, sizeof numbuf, val, 16, true);
            int pad_zero = (precision > len) ? (precision - len) : 0;
            if (!left_align)
                out_padding(ctx, width, len + pad_zero, zero_pad ? '0' : ' ');
            while (pad_zero-- > 0)
                out_char(ctx, '0');
            for (int i = 0; i < len; i++)
                out_char(ctx, numbuf[i]);
            if (left_align)
                out_padding(ctx, width, len + pad_zero, ' ');
            break;
        }
        case 'p':
        {
            unsigned long long val = (unsigned long long)va_arg(args, unsigned long);
            char numbuf[32];
            int hexlen = format_uint(numbuf, sizeof numbuf, val, 16, true);
            int total_len = 2 + hexlen;
            if (!left_align)
                out_padding(ctx, width, total_len, ' ');
            out_char(ctx, '0');
            out_char(ctx, 'x');
            for (int i = 0; i < hexlen; i++)
                out_char(ctx, numbuf[i]);
            if (left_align)
                out_padding(ctx, width, total_len, ' ');
            break;
        }
        case 's':
        {
            char *s = va_arg(args, char *);
            if (!s)
                s = "(null)";
            int len = (int)strlen(s);
            if (precision >= 0 && precision < len)
                len = precision;
            if (!left_align)
                out_padding(ctx, width, len, ' ');
            for (int i = 0; i < len; i++)
                out_char(ctx, s[i]);
            if (left_align)
                out_padding(ctx, width, len, ' ');
            break;
        }
        case 'c':
        {
            int c = va_arg(args, int);
            if (!left_align)
                out_padding(ctx, width, 1, ' ');
            out_char(ctx, (char)c);
            if (left_align)
                out_padding(ctx, width, 1, ' ');
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
            if (!left_align)
                out_padding(ctx, width, len, ' ');
            for (int i = 0; i < len; i++)
                out_char(ctx, buf[i]);
            if (left_align)
                out_padding(ctx, width, len, ' ');
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

int sprintf(char *buf, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int res = vsnprintf(buf, (size_t)-1, format, args);
    va_end(args);
    return res;
}

int vasprintf(char **strp, const char *fmt, va_list ap)
{
    char buf[1024];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof buf, fmt, ap2);
    va_end(ap2);
    if (n < 0)
        return -1;
    char *p = malloc((size_t)n + 1);
    if (!p)
        return -1;
    if (n < (int)sizeof(buf))
        memcpy(p, buf, (size_t)n + 1);
    else {
        va_copy(ap2, ap);
        vsnprintf(p, (size_t)n + 1, fmt, ap2);
        va_end(ap2);
    }
    *strp = p;
    return n;
}

int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int res = vprintf(format, args);
    va_end(args);
    return res;
}

int dprintf(int fd, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, format, args);
    va_end(args);
    if (n < 0)
        return -1;
    ssize_t w = write(fd, buf, (size_t)n);
    return (w >= 0 && (size_t)w == (size_t)n) ? n : -1;
}

int vprintf(const char *format, va_list args)
{
    struct out_ctx ctx = {
        .buf = nullptr,
        .size = 0,
        .pos = 0,
        .count = 0,
        .is_buffer = false,
    };
    vformat(&ctx, format, args);
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

        int width = 0;
        bool have_width = false;
        while (isdigit((unsigned char)*f))
        {
            have_width = true;
            width = width * 10 + (*f - '0');
            f++;
        }

        if (*f == 's')
        {
            while (isspace((unsigned char)*s))
                s++;
            if (*s == '\0')
                break;

            char *out = va_arg(args, char *);
            int copied = 0;
            bool limit = have_width && width > 0;
            while (*s && !isspace((unsigned char)*s) && (!limit || copied < width))
            {
                if (out)
                    out[copied] = *s;
                s++;
                copied++;
            }
            if (out)
                out[copied] = '\0';
            assigned++;
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

        char *endptr = nullptr;
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

int __isoc23_sscanf(const char *str, const char *format, ...) __attribute__((alias("sscanf")));

int getchar_blocking()
{
    int key = 0;
    key = getchar();
    while (key == 0)
    {
        key = getchar();
    }

    return key;
}
