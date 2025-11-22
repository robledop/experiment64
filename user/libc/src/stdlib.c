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
