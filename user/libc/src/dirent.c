#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stddef.h>

// Internal syscall wrapper; not part of the public libc API.
extern int sys_readdir(int fd, void *dent);

DIR *opendir(const char *name)
{
    const int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL;

    DIR *dir = malloc(sizeof(DIR));
    if (!dir)
    {
        close(fd);
        return NULL;
    }
    dir->fd = fd;
    return dir;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp)
        return NULL;
    const int res = sys_readdir(dirp->fd, &dirp->cur_entry);
    if (res != 1)
        return NULL; // 0 = EOF, -1 = Error
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
