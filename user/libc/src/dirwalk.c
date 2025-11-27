#include <dirwalk.h>
#include <dirent.h>
#include <string.h>

// Internal syscall wrapper from libc.
extern int sys_readdir(int fd, void *dent);

int dirwalk(int fd, int (*fn)(const struct dirent_view *entry, void *arg), void *arg)
{
    if (!fn)
        return -1;

    DIR dir = {.fd = fd};
    struct dirent *ent = &dir.cur_entry;

    while (sys_readdir(dir.fd, ent) == 1)
    {
        struct dirent_view view = {
            .name = ent->d_name,
            .name_len = strlen(ent->d_name),
            .inode = ent->d_ino,
        };
        int res = fn(&view, arg);
        if (res < 0)
            return -1;
    }
    return 0;
}
