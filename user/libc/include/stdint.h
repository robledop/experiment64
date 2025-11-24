#pragma once

typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef long long intptr_t;
typedef unsigned long long uintptr_t;

#define INTPTR_MAX ((intptr_t)0x7fffffffffffffffLL)
#define UINTPTR_MAX ((uintptr_t)0xffffffffffffffffULL)

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;
