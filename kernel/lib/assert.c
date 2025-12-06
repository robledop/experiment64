#include <assert.h>
#include <stdarg.h>
#include <terminal.h>
#include <debug.h>

void _assert(char* snippet, char* file, int line, char* message, ...)
{
    printk("\nassert failed %s:%d %s\n", file, line, snippet);

    if (*message)
    {
        va_list arg;
        va_start(arg, message);
        vprintk(message, arg);
        va_end(arg); // NOLINT(clang-analyzer-security.VAList) - va_start is called above
        panic(message);
    }
    panic("Assertion failed\n");
}
