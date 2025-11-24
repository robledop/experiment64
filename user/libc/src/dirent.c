#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>

DIR *opendir(const char *name)
{
    const int fd = open(name);
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
