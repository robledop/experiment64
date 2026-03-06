#pragma once

#include <sys/types.h>

struct group {
    char  *gr_name;
    char  *gr_passwd;
    int    gr_gid;
    char **gr_mem;
};

struct group *getgrgid(int gid);
struct group *getgrnam(const char *name);
void endgrent(void);
int initgroups(const char *user, gid_t gid);
