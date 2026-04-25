#pragma once
#include <dirent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termcolors.h>
#include <time.h>
#include <unistd.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 1024
#define _IONBF 0 // Not line buffered
#define _IOLBF 1 // Line buffered
#define _IOFBF 2 // Fully buffered

typedef struct stdio_file {
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
    char *wbuf;
    size_t wbuf_size;
    size_t wbuf_pos;
    int buf_mode;
    char wbuf_static[BUFSIZ];
} FILE;

extern struct stdio_file __stdin_file_obj;
extern struct stdio_file __stdout_file_obj;
extern struct stdio_file __stderr_file_obj;
extern FILE *__stdin_file;
extern FILE *__stdout_file;
extern FILE *__stderr_file;
#define stdin __stdin_file
#define stdout __stdout_file
#define stderr __stderr_file

int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int vsnprintf(char *restrict buf, size_t size, const char *restrict format, va_list args);
int vasprintf(char **strp, const char *fmt, va_list ap);
int snprintf(char *restrict buf, size_t size, const char *restrict format, ...);
int sprintf(char *restrict buf, const char *restrict format, ...);
int getchar(void);
int putchar(int c);
char *gets(char *s);
int puts(const char *s);
int sscanf(const char *str, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...);
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int getc_unlocked(FILE *stream);
int putc_unlocked(int c, FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets_unlocked(char *s, int n, FILE *stream);
int fputs_unlocked(const char *s, FILE *stream);
int putchar_unlocked(int c);
int fileno(FILE *stream);
int fileno_unlocked(FILE *stream);
void clearerr(FILE *stream);
int dprintf(int fd, const char *format, ...);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
int fseeko(FILE *stream, off_t offset, int whence);
long ftell(FILE *stream);
off_t ftello(FILE *stream);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fflush(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);
int ferror(FILE *stream);
int ferror_unlocked(FILE *stream);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
int getchar_blocking();
