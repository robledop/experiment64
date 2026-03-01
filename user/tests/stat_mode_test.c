#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    const char *path = "/stat_mode_test.txt";

    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return 1;
    if (write(fd, "x", 1) != 1)
        return 2;
    if (close(fd) != 0)
        return 3;

    struct stat st = {0};
    if (stat(path, &st) != 0)
        return 4;
    if (!S_ISREG(st.st_mode))
        return 5;
    if (st.st_size != 1)
        return 6;

    if (stat("/", &st) != 0)
        return 7;
    if (!S_ISDIR(st.st_mode))
        return 8;

    if (unlink(path) != 0)
        return 9;
    return 0;
}
