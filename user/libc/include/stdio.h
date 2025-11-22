#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

int printf(const char *format, ...);
int getchar(void);
int putchar(int c);
char *gets(char *s);

#endif
