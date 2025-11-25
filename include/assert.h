#pragma once

void _assert(char *snippet, char *file, int line, char *message, ...);

#ifdef DEBUG
#define assert(cond, ...)                                                                                              \
if (!(cond))                                                                                                       \
_assert(#cond, __FILE__, __LINE__, #__VA_ARGS__ __VA_OPT__(, )##__VA_ARGS__)
#else
#define assert(cond, ...)
#endif
