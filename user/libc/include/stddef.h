#pragma once

#define NULL ((void *)0)

typedef unsigned long long size_t;
typedef long long ptrdiff_t;

#define offsetof(type, member) __builtin_offsetof(type, member)
