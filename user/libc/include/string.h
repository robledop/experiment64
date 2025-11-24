#ifndef _STRING_H
#define _STRING_H

#ifdef __clang__
#pragma clang system_header
#endif

#include <stddef.h>

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
char *strtok(char *str, const char *delim);
char *strchr(const char *s, int c);

#endif
