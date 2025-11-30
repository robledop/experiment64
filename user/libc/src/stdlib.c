#include <stdlib.h>

#include "stdio.h"

static void (*atexit_handlers[32])(void);
static int atexit_count = 0;

int atexit(void (*func)(void))
{
    if (!func || atexit_count >= (int)(sizeof(atexit_handlers) / sizeof(atexit_handlers[0])))
        return -1;
    atexit_handlers[atexit_count++] = func;
    return 0;
}

void __libc_run_atexit(void)
{
    for (int i = atexit_count - 1; i >= 0; --i)
    {
        if (atexit_handlers[i])
            atexit_handlers[i]();
    }
}

void _Exit(int status)
{
    __exit_impl(status);
}

int system([[maybe_unused]] const char *command)
{
    // No shell; stub returns failure.
    return -1;
}

int atoi(const char *nptr)
{
    int res = 0;
    int sign = 1;
    if (*nptr == '-')
    {
        sign = -1;
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9')
    {
        res = res * 10 + (*nptr - '0');
        nptr++;
    }
    return res * sign;
}


int abs(int x)
{
    return x < 0 ? -x : x;
}

long strtol(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    while (*p == ' ' || *p == '\t')
        p++;

    int sign = 1;
    if (*p == '+' || *p == '-')
    {
        if (*p == '-')
            sign = -1;
        p++;
    }

    int actual_base = base;
    if (actual_base == 0)
    {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        {
            actual_base = 16;
            p += 2;
        }
        else if (p[0] == '0')
        {
            actual_base = 8;
            p++;
        }
        else
        {
            actual_base = 10;
        }
    }
    else if (actual_base == 16)
    {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            p += 2;
    }
    else if (actual_base != 8 && actual_base != 10)
    {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    long result = 0;
    const char *start_digits = p;
    while (*p)
    {
        int digit;
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f')
            digit = 10 + (*p - 'a');
        else if (*p >= 'A' && *p <= 'F')
            digit = 10 + (*p - 'A');
        else
            break;

        if (digit >= actual_base)
            break;

        result = result * actual_base + digit;
        p++;
    }

    if (p == start_digits)
    {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    if (endptr)
        *endptr = (char *)p;

    return result * sign;
}

double atof(const char *nptr)
{
    if (!nptr)
        return 0.0;
    double result = 0.0;
    double sign = 1.0;
    if (*nptr == '-')
    {
        sign = -1.0;
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9')
    {
        result = result * 10.0 + (double)(*nptr - '0');
        nptr++;
    }
    if (*nptr == '.')
    {
        nptr++;
        double place = 0.1;
        while (*nptr >= '0' && *nptr <= '9')
        {
            result += place * (double)(*nptr - '0');
            place *= 0.1;
            nptr++;
        }
    }
    return result * sign;
}

void panic(const char *s)
{
    printf("panic: %s\n", s);
    exit(1);
}
