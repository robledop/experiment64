#pragma once

#include <stddef.h>

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
} glob_t;

#define GLOB_APPEND   0x01
#define GLOB_DOOFFS   0x02
#define GLOB_ERR      0x04
#define GLOB_MARK     0x08
#define GLOB_NOCHECK  0x10
#define GLOB_NOESCAPE 0x20
#define GLOB_NOSORT   0x40

#define GLOB_ABORTED  1
#define GLOB_NOMATCH  2
#define GLOB_NOSPACE  3

int glob(const char *pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *pglob);
void globfree(glob_t *pglob);
