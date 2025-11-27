#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>

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
    {
        return (unsigned char)c;
    }
    return EOF;
}

char *gets(char *s)
{
    char *p = s;
    int c;
    while ((c = getchar()) != EOF && c != '\n')
    {
        *p++ = (char)c;
    }
    *p = '\0';
    if (c == EOF && p == s)
        return nullptr;
    return s;
}

static int print_uint(unsigned long n)
{
    char buf[32];
    int i = 0;
    if (n == 0)
    {
        putchar('0');
        return 1;
    }
    while (n > 0 && i < (int)sizeof(buf))
    {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    int written = i;
    while (i-- > 0)
        putchar(buf[i]);
    return written;
}

static int print_int(long n)
{
    int written = 0;
    if (n < 0)
    {
        putchar('-');
        written++;
        n = -n;
    }
    return written + print_uint((unsigned long)n);
}

static int print_hex(unsigned long n)
{
    if (n >= 16)
        print_hex(n / 16);
    int rem = (int)(n % 16);
    char c = (rem < 10) ? (char)('0' + rem) : (char)('a' + (rem - 10));
    putchar(c);
    return 1; // caller manages width if needed
}

static int print_padding(int width, int content_len, char pad_char)
{
    int pads = width - content_len;
    if (pads < 0)
        pads = 0;
    for (int i = 0; i < pads; i++)
        putchar(pad_char);
    return pads;
}

int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int count = 0;

    for (const char *p = format; *p; p++)
    {
        if (*p != '%')
        {
            putchar(*p);
            count++;
            continue;
        }
        p++;
        // Parse minimal width (decimal digits) and a single 'l' length modifier.
        int width = 0;
        bool long_mod = false;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            p++;
        }
        if (*p == 'l')
        {
            long_mod = true;
            p++;
        }

        switch (*p)
        {
        case 'd':
        {
            long val = long_mod ? va_arg(args, long) : va_arg(args, int);
            // Capture output length by printing into a temp buffer via recursion? Simpler: format into stack buffer.
            char tmp[32];
            int len = 0;
            long n = val;
            if (n < 0)
            {
                tmp[len++] = '-';
                n = -n;
            }
            // write number into buffer reversed
            char numbuf[32];
            int ni = 0;
            if (n == 0)
                numbuf[ni++] = '0';
            while (n > 0 && ni < (int)sizeof(numbuf))
            {
                numbuf[ni++] = (char)('0' + (n % 10));
                n /= 10;
            }
            // now emit padding then buffer
            int total_len = len + ni;
            count += print_padding(width, total_len, ' ');
            for (int j = 0; j < len; j++)
            {
                putchar(tmp[j]);
                count++;
            }
            while (ni-- > 0)
            {
                putchar(numbuf[ni]);
                count++;
            }
            break;
        }
        case 's':
        {
            char *s = va_arg(args, char *);
            if (!s)
                s = "(null)";
            while (*s)
            {
                putchar(*s++);
                count++;
            }
            break;
        }
        case 'c':
        {
            int c = va_arg(args, int);
            putchar(c);
            count++;
            break;
        }
        case 'p':
        {
            unsigned long ptr = va_arg(args, unsigned long);
            putchar('0');
            putchar('x');
            print_hex(ptr);
            // crude width handling: prepend padding only, since pointer printed last
            break;
        }
        case 'x':
        {
            unsigned long ptr = va_arg(args, unsigned long);
            // hex width: pad based on hex digit count
            char numbuf[32];
            int ni = 0;
            if (ptr == 0)
                numbuf[ni++] = '0';
            unsigned long tmp = ptr;
            while (tmp > 0 && ni < (int)sizeof(numbuf))
            {
                int rem = (int)(tmp % 16);
                numbuf[ni++] = (rem < 10) ? (char)('0' + rem) : (char)('a' + (rem - 10));
                tmp /= 16;
            }
            count += print_padding(width, ni, ' ');
            while (ni-- > 0)
            {
                putchar(numbuf[ni]);
                count++;
            }
            break;
        }
        default:
            putchar('%');
            if (*p)
            {
                putchar(*p);
                count += 2;
            }
            else
            {
                count += 1;
                p--; // so outer loop terminates
            }
            break;
        }
    }
    va_end(args);
    return count;
}

int puts(const char *s)
{
    while (*s)
    {
        putchar(*s++);
    }
    putchar('\n');
    return 0;
}
