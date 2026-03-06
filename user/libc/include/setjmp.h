#pragma once

#include <stdint.h>

typedef intptr_t jmp_buf[5];
typedef intptr_t sigjmp_buf[5];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
int sigsetjmp(sigjmp_buf env, int savemask);
void siglongjmp(sigjmp_buf env, int val);
