#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

static struct termios termios_table[16];
static bool termios_initialized = false;

static int map_fd(int fd)
{
    if (fd < 0)
        return fd;
    if (fd <= 2)
        return 0; // stdin/stdout/stderr share the same TTY attributes
    return fd;
}

static void init_defaults(void)
{
    if (termios_initialized)
        return;

    struct termios def = {
        .c_iflag = IXON | ICRNL,
        .c_oflag = OPOST,
        .c_cflag = 0,
        .c_lflag = ECHO | ICANON,
    };
    def.c_cc[VMIN] = 1;
    def.c_cc[VTIME] = 0;

    for (size_t i = 0; i < (sizeof(termios_table) / sizeof(termios_table[0])); i++)
    {
        termios_table[i] = def;
    }

    termios_initialized = true;
}

static struct termios *get_entry(int fd)
{
    int idx = map_fd(fd);
    if (idx < 0 || idx >= (int)(sizeof(termios_table) / sizeof(termios_table[0])))
        return nullptr;
    init_defaults();
    return &termios_table[idx];
}

tcflag_t __termios_get_oflag(int fd)
{
    struct termios *entry = get_entry(fd);
    return entry ? entry->c_oflag : 0;
}

void cfmakeraw(struct termios *termios_p)
{
    if (!termios_p)
        return;

    termios_p->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_cflag &= ~CSIZE;
    termios_p->c_cflag |= CS8;
    termios_p->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    termios_p->c_cc[VMIN]  = 1;
    termios_p->c_cc[VTIME] = 0;
}

int tcgetattr(int fd, struct termios *termios_p)
{
    if (!termios_p)
        return -1;

    if (ioctl(fd, TIOCGETA, termios_p) != 0)
        return -1;

    struct termios *entry = get_entry(fd);
    if (entry)
        memcpy(entry, termios_p, sizeof(struct termios));
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    if (!termios_p)
        return -1;
    int request = 0;
    if (optional_actions == TCSANOW) {
        request = TCSETS;
    } else if (optional_actions == TCSADRAIN) {
        request = TCSETSW;
    } else     if (optional_actions == TCSAFLUSH) {
        request = TCSETSF;
    } else {
        errno = EINVAL;
        return -1;
    }

    if (ioctl(fd, request, (void *)termios_p) != 0)
        return -1;

    struct termios *entry = get_entry(fd);
    if (entry)
        memcpy(entry, termios_p, sizeof(struct termios));
    return 0;
}

int tcflush(int fd, int queue_selector)
{
    (void)fd;
    (void)queue_selector;
    return 0;
}
