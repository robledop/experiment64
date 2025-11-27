#pragma once

#include <stdint.h>

#define T_FILE 1
#define T_DIR  2
#define T_DEV  3

struct stat
{
    int dev;
    int ino;
    int type;
    int nlink;
    uint64_t size;
    int ref;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    int i_uid;
    int i_gid;
    int i_flags;
};
