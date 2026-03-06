#pragma once

#include <stddef.h>

#ifdef __GNUC__
#define alloca(size) __builtin_alloca(size)
#else
void *alloca(size_t size);
#endif
