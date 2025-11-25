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
        char* data = va_arg(arg, char *);
        printk(data, arg);
        panic(message);
    }
    panic("Assertion failed\n");
}
