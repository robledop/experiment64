#pragma once

#ifdef __clang__
#pragma clang system_header
#endif

#include <stddef.h>
#include <stdbool.h>

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memmove(void *dest, const void *src, size_t n);
char *strtok(char *str, const char *delim);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
bool starts_with(const char pre[static 1], const char str[static 1]);
bool ends_with(const char *str, const char *suffix);
char *strcat(char dest[static 1], const char src[static 1]);
char *strncat(char dest[static 1], const char src[static 1], size_t n);
void reverse(char *s);
char *strdup(const char *s);
char *strstr(const char *haystack, const char *needle);
