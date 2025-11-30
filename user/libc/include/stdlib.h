#pragma once

#ifdef __clang__
#pragma clang system_header
#endif

#include <stddef.h>

void __exit_with_handlers(int status);
[[noreturn]] void exit(int status);
[[noreturn]] void _Exit(int status);
[[noreturn]] void __exit_impl(int status);
int atexit(void (*func)(void));
void __libc_run_atexit(void);

#define exit(...) __exit_with_handlers(__VA_ARGS__ + 0)

int system(const char *command);
int atoi(const char *nptr);
int abs(int x);
long strtol(const char *nptr, char **endptr, int base);
double atof(const char *nptr);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void panic(const char *s);
