#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    const char *posix_path = "/open_compat_posix.txt";
    const char *legacy_path = "/open_compat_legacy.txt";
    char c = 0;

    int fd = open(posix_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return 1;
    if (write(fd, "A", 1) != 1)
        return 2;
    if (close(fd) != 0)
        return 3;

    fd = open(posix_path, O_RDONLY);
    if (fd < 0)
        return 4;
    if (read(fd, &c, 1) != 1)
        return 5;
    if (c != 'A')
        return 6;
    if (close(fd) != 0)
        return 7;

    fd = open(legacy_path, O_CREATE | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return 8;
    if (write(fd, "B", 1) != 1)
        return 9;
    if (close(fd) != 0)
        return 10;

    fd = open(legacy_path, O_RDONLY);
    if (fd < 0)
        return 11;
    if (read(fd, &c, 1) != 1)
        return 12;
    if (c != 'B')
        return 13;
    if (close(fd) != 0)
        return 14;

    if (unlink(posix_path) != 0)
        return 15;
    if (unlink(legacy_path) != 0)
        return 16;

    return 0;
}
