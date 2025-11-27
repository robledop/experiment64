#include <string.h>

size_t strlen(const char* s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dest, const char* src)
{
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;

    for (; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }

    for (; i < n; i++) {
        dst[i] = '\0';
    }

    return dst;
}

void* memset(void* s, int c, size_t n)
{
    unsigned char* p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    char* d = dest;
    const char* s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
    char* d = (char*)dest;
    const char* s = (const char*)src;
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

char* strchr(const char* s, int c)
{
    while (*s != (char)c)
    {
        if (!*s++)
        {
            return nullptr;
        }
    }
    return (char*)s;
}

char* strtok(char* str, const char* delim)
{
    static char* next_token = nullptr;
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

    char* start = next_token;
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

bool starts_with(const char pre[static 1], const char str[static 1])
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

char* strcat(char dest[static 1], const char src[static 1])
{
    char* d = dest;
    const char* s = src;
    while (*d != '\0')
    {
        d++;
    }
    while (*s != '\0')
    {
        *d++ = *s++;
    }
    *d = '\0';
    return dest;
}

char* strncat(char dest[static 1], const char src[static 1], size_t n)
{
    char* d = dest;
    const char* s = src;
    size_t copied = 0;
    while (*d != '\0')
    {
        d++;
    }
    while (copied < n && *s != '\0')
    {
        *d++ = *s++;
        copied++;
    }
    *d = '\0';
    return dest;
}

void reverse(char* s)
{
    int i, j;

    for (i = 0, j = (int)strlen(s) - 1; i < j; i++, j--)
    {
        const char c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}
