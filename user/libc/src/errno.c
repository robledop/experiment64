#include <errno.h>
#include <stdio.h>
#include <status.h>

static thread_local int g_errno;

int *__errno_location(void)
{
    return &g_errno;
}

void perror(const char *s)
{
    printf("%s %s\n", s, strerror(errno));
}
