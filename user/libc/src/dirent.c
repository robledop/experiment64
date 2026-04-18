#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

// Internal syscall wrapper; not part of the public libc API.
extern int sys_readdir(int fd, void *dent);

DIR *opendir(const char *name)
{
    const int fd = open(name, O_RDONLY);
    if (fd < 0)
        return nullptr;

    DIR *dir = malloc(sizeof(DIR));
    if (!dir)
    {
        close(fd);
        return nullptr;
    }
    dir->fd = fd;
    return dir;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp)
        return nullptr;
    const int res = sys_readdir(dirp->fd, &dirp->cur_entry);
    if (res != 1)
        return nullptr; // 0 = EOF, -1 = Error
    return &dirp->cur_entry;
}

int closedir(DIR *dirp)
{
    if (!dirp)
        return -1;
    close(dirp->fd);
    free(dirp);
    return 0;
}

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
