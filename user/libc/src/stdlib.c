#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <status.h>

static void (*atexit_handlers[32])(void);
static int atexit_count = 0;

int atexit(void (*func)(void))
{
    if (!func || atexit_count >= (int)(sizeof(atexit_handlers) / sizeof(atexit_handlers[0]))) {
        return -1;
    }
    atexit_handlers[atexit_count++] = func;
    return 0;
}

void __libc_run_atexit(void)
{
    for (int i = atexit_count - 1; i >= 0; --i) {
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
    return -1;
}

char *getenv(const char *name)
{
    (void)name;
    return nullptr;
}

int putenv(char *string)
{
    (void)string;
    return 0;
}

int unsetenv(const char *name)
{
    (void)name;
    return 0;
}

int clearenv(void)
{
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name;
    (void)value;
    (void)overwrite;
    return 0;
}

int mkstemp(char *template)
{
    (void)template;
    errno = EUNIMP;
    return -1;
}

char *realpath(const char *path, char *resolved_path)
{
    if (!path)
        return nullptr;
    if (resolved_path)
        return strcpy(resolved_path, path);
    return strdup(path);
}

static unsigned int rand_seed;

void srand(unsigned int seed)
{
    rand_seed = seed;
}

int rand(void)
{
    rand_seed = rand_seed * 1103515245u + 12345u;
    return (int)(rand_seed / 65536u % 32768u);
}

int atoi(const char *nptr)
{
    int res  = 0;
    int sign = 1;
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
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
    if (*p == '+' || *p == '-') {
        if (*p == '-')
            sign = -1;
        p++;
    }

    int actual_base = base;
    if (actual_base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            actual_base = 16;
            p += 2;
        } else if (p[0] == '0') {
            actual_base = 8;
            p++;
        } else {
            actual_base = 10;
        }
    } else if (actual_base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            p += 2;
    } else if (actual_base != 8 && actual_base != 10) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    long result              = 0;
    const char *start_digits = p;
    while (*p) {
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

    if (p == start_digits) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0;
    }

    if (endptr)
        *endptr = (char *)p;

    return result * sign;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '+')
        p++;
    int actual_base = base;
    if (actual_base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            actual_base = 16;
            p += 2;
        } else if (p[0] == '0') {
            actual_base = 8;
            p++;
        } else {
            actual_base = 10;
        }
    } else if (actual_base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    unsigned long result = 0;
    const char *start = p;
    while (*p) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = 10 + (*p - 'a');
        else if (*p >= 'A' && *p <= 'F') d = 10 + (*p - 'A');
        else break;
        if (d >= actual_base) break;
        if (result > ((unsigned long)-1 - (unsigned long)d) / (unsigned long)actual_base) {
            errno = ERANGE;
            result = (unsigned long)-1;
            p++;
            while (*p && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
                p++;
            if (endptr) *endptr = (char *)p;
            return result;
        }
        result = result * (unsigned long)actual_base + (unsigned long)d;
        p++;
    }
    if (endptr) *endptr = (char *)p;
    return p == start ? 0 : result;
}

long long strtoll(const char *nptr, char **endptr, int base)
{
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    return (unsigned long long)strtoul(nptr, endptr, base);
}

double atof(const char *nptr)
{
    if (!nptr)
        return 0.0;
    double result = 0.0;
    double sign   = 1.0;
    if (*nptr == '-') {
        sign = -1.0;
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10.0 + (double)(*nptr - '0');
        nptr++;
    }
    if (*nptr == '.') {
        nptr++;
        double place = 0.1;
        while (*nptr >= '0' && *nptr <= '9') {
            result += place * (double)(*nptr - '0');
            place *= 0.1;
            nptr++;
        }
    }
    return result * sign;
}

static void qsort_swap(char *a, char *b, size_t size)
{
    char tmp[256];
    while (size > 256)
    {
        memcpy(tmp, a, 256);
        memcpy(a, b, 256);
        memcpy(b, tmp, 256);
        a += 256;
        b += 256;
        size -= 256;
    }
    memcpy(tmp, a, size);
    memcpy(a, b, size);
    memcpy(b, tmp, size);
}

static void qsort_inner(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *), char *tmp)
{
    if (nmemb <= 1)
        return;
    char *b = base;
    size_t pi = nmemb / 2;
    char *pivot = b + pi * size;
    qsort_swap(b, pivot, size);
    size_t i = 1;
    size_t j = nmemb - 1;
    while (i <= j)
    {
        while (i <= j && compar(b + i * size, b) <= 0)
            i++;
        while (i <= j && compar(b + j * size, b) > 0)
            j--;
        if (i < j)
            qsort_swap(b + i * size, b + j * size, size);
    }
    qsort_swap(b, b + j * size, size);
    if (j > 0)
        qsort_inner(b, j, size, compar, tmp);
    if (j + 1 < nmemb)
        qsort_inner(b + (j + 1) * size, nmemb - (j + 1), size, compar, tmp);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    const char *p = (const char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = compar(key, p + mid * size);
        if (c < 0)
            hi = mid;
        else if (c > 0)
            lo = mid + 1;
        else
            return (void *)(p + mid * size);
    }
    return nullptr;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    if (!base || nmemb == 0 || size == 0 || !compar)
        return;
    char tmp[256];
    qsort_inner(base, nmemb, size, compar, tmp);
}

[[noreturn]] void panic(const char *s)
{
    printf("panic: %s\n", s);
    exit(1);
}
