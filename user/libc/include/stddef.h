#pragma once

#define NULL ((void *)0)

typedef unsigned long size_t;
typedef long ptrdiff_t;

// ReSharper disable once CppInconsistentNaming
#define offsetof(type, member) __builtin_offsetof(type, member)

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define nullptr ((void *)0)
#endif
