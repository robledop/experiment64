#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <termcolors.h>
#include <dirwalk.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define EOF (-1)

int printf(const char *format, ...);
int vsnprintf(char *restrict buf, size_t size, const char *restrict format, va_list args);
int snprintf(char *restrict buf, size_t size, const char *restrict format, ...);
int getchar(void);
int putchar(int c);
char *gets(char *s);

#endif
