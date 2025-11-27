#pragma once

#include <stddef.h>
#include <stdarg.h>

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define nullptr ((void *)0)
#endif

int strncmp(const char *s1, const char *s2, size_t n);
int strcmp(const char *s1, const char *s2);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strrchr(const char *s, int c);

typedef void (*printf_callback_t)(char c, void *arg);
int vcbprintf(void *arg, printf_callback_t callback, const char *format, va_list *args);

int vsnprintk(char *buffer, size_t size, const char *format, va_list args);
int snprintk(char *buffer, size_t size, const char *format, ...);
