#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

void _assert(char *snippet, char *file, int line, char *message, ...)
{
    va_list arg = {};
    va_start(arg, message);

    printf("\nassert failed %s:%d %s\n", file, line, snippet);

    if (*message) {
        vprintf(message, arg);
        panic(message);
    }
    panic("Assertion failed\n");
    va_end(arg);
}
