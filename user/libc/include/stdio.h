#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <termcolors.h>
#include <dirwalk.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct FILE
{
    int fd;
    bool readable;
    bool writable;
    bool append;
    bool need_seek;
    char *data;
    size_t size;
    size_t pos;
    int open_flags;
    char path[128];
} FILE;

extern FILE __stdin_file_obj;  // NOLINT(misc-non-copyable-objects)
extern FILE __stdout_file_obj; // NOLINT(misc-non-copyable-objects)
extern FILE __stderr_file_obj; // NOLINT(misc-non-copyable-objects)
extern FILE *__stdin_file;
extern FILE *__stdout_file;
extern FILE *__stderr_file;
#define stdin __stdin_file
#define stdout __stdout_file
#define stderr __stderr_file

int printf(const char *format, ...);
int vsnprintf(char *restrict buf, size_t size, const char *restrict format, va_list args);
int snprintf(char *restrict buf, size_t size, const char *restrict format, ...);
int getchar(void);
int putchar(int c);
char *gets(char *s);
int puts(const char *s);
int sscanf(const char *str, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
int getchar_blocking();
