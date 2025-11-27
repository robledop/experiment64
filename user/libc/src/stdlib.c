#include <stdlib.h>

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
    if (base != 0 && base != 10)
    {
        // Minimal implementation: support base 10 only
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

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

    long result = 0;
    while (*p >= '0' && *p <= '9')
    {
        result = result * 10 + (*p - '0');
        p++;
    }

    if (endptr)
        *endptr = (char *)p;

    return result * sign;
}
