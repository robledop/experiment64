#pragma once

#define FNM_NOMATCH 1
#define FNM_PATHNAME 1
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4

int fnmatch(const char *pattern, const char *string, int flags);
