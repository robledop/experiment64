#include <setjmp.h>

int setjmp(jmp_buf env)
{
    return __builtin_setjmp((void **)env);
}

void longjmp(jmp_buf env, int val)
{
    (void)val;
    __builtin_longjmp((void **)env, 1);
}

int sigsetjmp(sigjmp_buf env, int savemask)
{
    (void)savemask;
    return __builtin_setjmp((void **)env);
}

void siglongjmp(sigjmp_buf env, int val)
{
    (void)val;
    __builtin_longjmp((void **)env, 1);
}
