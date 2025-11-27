#include <string.h>

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
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

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *d = dest;
    const char *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d == s || n == 0)
        return dest;

    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else
    {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

char *strchr(const char *s, int c)
{
    while (*s != (char)c)
    {
        if (!*s++)
        {
            return nullptr;
        }
    }
    return (char *)s;
}

char *strtok(char *str, const char *delim)
{
    static char *next_token = nullptr;
    if (str)
        next_token = str;
    if (!next_token)
        return nullptr;

    // Skip leading delimiters
    while (*next_token)
    {
        if (strchr(delim, *next_token) == nullptr)
            break;
        next_token++;
    }

    if (!*next_token)
        return nullptr;

    char *start = next_token;
    while (*next_token)
    {
        if (strchr(delim, *next_token))
        {
            *next_token = '\0';
            next_token++;
            return start;
        }
        next_token++;
    }
    return start;
}
