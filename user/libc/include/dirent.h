#ifndef _DIRENT_H
#define _DIRENT_H

#include <stdint.h>

struct dirent
{
    char d_name[128];
    uint32_t d_ino;
};

typedef struct
{
    int fd;
    struct dirent cur_entry; // Buffer for readdir
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#endif
