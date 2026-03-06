#pragma once

#define UNUSED __attribute__((unused))
#define USED __attribute__((used))
#define NONNULL __attribute__((nonnull))
#if !defined(NORETURN)
#define NORETURN __attribute__((noreturn))
#endif
