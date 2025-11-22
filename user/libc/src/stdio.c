#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

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
        *p++ = c;
    }
    *p = '\0';
    if (c == EOF && p == s)
        return NULL;
    return s;
}

static void print_int(int n)
{
    if (n < 0)
    {
        putchar('-');
        n = -n;
    }
    if (n / 10)
        print_int(n / 10);
    putchar((n % 10) + '0');
}

static void print_hex(unsigned long n)
{
    if (n / 16)
        print_hex(n / 16);
    int rem = n % 16;
    if (rem < 10)
        putchar(rem + '0');
    else
        putchar(rem - 10 + 'a');
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
        switch (*p)
        {
        case 'd':
        {
            int i = va_arg(args, int);
            print_int(i);
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
        case 'x':
        {
            unsigned long x = va_arg(args, unsigned long);
            print_hex(x);
            break;
        }
        default:
            putchar('%');
            putchar(*p);
            count += 2;
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
