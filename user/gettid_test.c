#include <unistd.h>
#include <stdio.h>

int main(void)
{
    int tid = gettid();
    if (tid <= 0)
        return 1;
    printf("tid: %d\n", tid);
    return 0;
}
