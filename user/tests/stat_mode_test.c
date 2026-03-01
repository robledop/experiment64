#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
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

    fd = open("/dev/console", O_RDONLY);
    if (fd < 0)
        return 10;

    struct termios saved = {0};
    if (tcgetattr(fd, &saved) != 0) {
        close(fd);
        return 11;
    }

    struct termios raw = saved;
    cfmakeraw(&raw);
    if (tcsetattr(fd, TCSANOW, &raw) != 0) {
        close(fd);
        return 12;
    }
    if (tcsetattr(fd, TCSADRAIN, &saved) != 0) {
        close(fd);
        return 13;
    }
    if (tcsetattr(fd, TCSAFLUSH, &saved) != 0) {
        close(fd);
        return 14;
    }

    errno = 0;
    if (tcsetattr(fd, 99, &saved) != -1) {
        close(fd);
        return 15;
    }
    if (errno != EINVAL) {
        close(fd);
        return 16;
    }
    if (close(fd) != 0)
        return 17;

    return 0;
}
