#pragma once

#include <stddef.h>
#include <stdarg.h>

int strncmp(const char *s1, const char *s2, size_t n);
int strcmp(const char *s1, const char *s2);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
