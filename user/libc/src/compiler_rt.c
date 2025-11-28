#include <stdint.h>

long long __divdi3(long long a, long long b)
{
    return a / b;
}

long long __moddi3(long long a, long long b)
{
    return a % b;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    return a / b;
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    return a % b;
}
