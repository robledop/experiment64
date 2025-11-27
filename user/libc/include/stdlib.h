#ifndef _STDLIB_H
#define _STDLIB_H

#ifdef __clang__
#pragma clang system_header
#endif

#include <stddef.h>

void __exit_impl(int status);
#define exit(...) __exit_impl(__VA_ARGS__ + 0)
int atoi(const char *nptr);
int abs(int x);
long strtol(const char *nptr, char **endptr, int base);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

#endif
