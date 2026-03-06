#pragma once

#ifdef __clang__
#pragma clang system_header
#endif

#include <stddef.h>
#include <alloca.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

[[noreturn]] void __exit_with_handlers(int status);
[[noreturn]] void exit(int status);
[[noreturn]] void _Exit(int status);
[[noreturn]] void __exit_impl(int status);
int atexit(void (*func)(void));
void __libc_run_atexit(void);

#define exit(...) __exit_with_handlers(__VA_ARGS__ + 0)

int system(const char* command);
int atoi(const char* nptr);
int abs(int x);
long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double atof(const char* nptr);

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
char *getenv(const char *name);
int putenv(char *string);
int unsetenv(const char *name);
int clearenv(void);
int setenv(const char *name, const char *value, int overwrite);
int mkstemp(char *template);
char *realpath(const char *path, char *resolved_path);
void srand(unsigned int seed);
int rand(void);
void panic(const char* s);
