#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>

static void *memcpy_impl(void *restrict dst, const void *restrict src, size_t n);

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

size_t strnlen(const char *s, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && s[n] != '\0')
    {
        n++;
    }
    return n;
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

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;

    for (; i < n && src[i] != '\0'; i++)
    {
        dst[i] = src[i];
    }

    for (; i < n; i++)
    {
        dst[i] = '\0';
    }

    return dst;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    unsigned char byte = (unsigned char)c;

    // Build 64-bit pattern
    uint64_t val = byte;
    val |= val << 8;
    val |= val << 16;
    val |= val << 32;

    // Use rep stosq for 8-byte aligned bulk fills
    if (((uintptr_t)p & 7) == 0 && n >= 8)
    {
        size_t qwords = n / 8;
        __asm__ volatile(
            "rep stosq"
            : "+D"(p), "+c"(qwords)
            : "a"(val)
            : "memory");
        n &= 7; // remaining bytes
    }

    // Handle remaining bytes with rep stosb
    if (n > 0)
    {
        __asm__ volatile(
            "rep stosb"
            : "+D"(p), "+c"(n)
            : "a"(byte)
            : "memory");
    }

    return s;
}

static void *memcpy_impl(void *restrict dst, const void *restrict src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    // Handle unaligned head
    while (n && ((uintptr_t)d & 31))
    {
        *d++ = *s++;
        n--;
    }

    // Copy 32 bytes at a time using AVX if source is also aligned
    if (((uintptr_t)s & 31) == 0)
    {
        while (n >= 32)
        {
            __asm__ volatile(
                "vmovdqa ymm0, [%1]\n\t"
                "vmovdqa [%0], ymm0\n\t"
                : : "r"(d), "r"(s) : "ymm0", "memory");
            d += 32;
            s += 32;
            n -= 32;
        }
    }
    else
    {
        // Unaligned source - use unaligned loads
        while (n >= 32)
        {
            __asm__ volatile(
                "vmovdqu ymm0, [%1]\n\t"
                "vmovdqa [%0], ymm0\n\t"
                : : "r"(d), "r"(s) : "ymm0", "memory");
            d += 32;
            s += 32;
            n -= 32;
        }
    }

    // Handle remaining bytes
    while (n--)
    {
        *d++ = *s++;
    }

    return dst;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    return memcpy_impl(dest, src, n);
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--)
    {
        if (*p1 != *p2)
        {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t n)
{
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d == s || n == 0)
        return dest;

    // Safe to use AVX forward copy: no overlap, or dest is before src
    if (d < s || (uintptr_t)d >= (uintptr_t)s + n)
    {
        return memcpy_impl(dest, src, n);
    }

    // Overlapping backward copy required
    d += n;
    s += n;
    // Use 64-bit when aligned
    if (((uintptr_t)d & 7) == 0 && ((uintptr_t)s & 7) == 0)
    {
        uint64_t *d64 = (uint64_t *)d;
        const uint64_t *s64 = (const uint64_t *)s;
        while (n >= 8)
        {
            *--d64 = *--s64;
            n -= 8;
        }
        d = (char *)d64;
        s = (const char *)s64;
    }
    while (n--)
        *--d = *--s;
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

bool starts_with(const char pre[static 1], const char str[static 1])
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

char *strcat(char dest[static 1], const char src[static 1])
{
    char *d = dest;
    const char *s = src;
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

char *strncat(char dest[static 1], const char src[static 1], size_t n)
{
    char *d = dest;
    const char *s = src;
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

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n > 0 && *s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

char *strdup(const char *s)
{
    if (!s)
        return nullptr;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (!p)
        return nullptr;
    memcpy(p, s, len);
    return p;
}

char *strrchr(const char *s, int c)
{
    const char *last = nullptr;
    while (*s)
    {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle)
        return (char *)haystack;
    for (const char *h = haystack; *h; h++)
    {
        const char *p = h;
        const char *n = needle;
        while (*p && *n && *p == *n)
        {
            p++;
            n++;
        }
        if (*n == '\0')
            return (char *)h;
    }
    return nullptr;
}

void reverse(char *s)
{
    int i, j;

    for (i = 0, j = (int)strlen(s) - 1; i < j; i++, j--)
    {
        const char c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

bool ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix)
    {
        return false;
    }

    const size_t str_len = strlen(str);
    const size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len)
    {
        return false;
    }

    return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}
