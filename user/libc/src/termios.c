#include <termios.h>
#include <string.h>
#include <stdbool.h>

static struct termios termios_table[16];
static bool termios_initialized = false;

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
    if (fd < 0 || fd >= (int)(sizeof(termios_table) / sizeof(termios_table[0])))
        return NULL;
    init_defaults();
    return &termios_table[fd];
}

int tcgetattr(int fd, struct termios *termios_p)
{
    if (!termios_p)
        return -1;
    struct termios *entry = get_entry(fd);
    if (!entry)
        return -1;
    memcpy(termios_p, entry, sizeof(struct termios));
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    (void)optional_actions;
    if (!termios_p)
        return -1;
    struct termios *entry = get_entry(fd);
    if (!entry)
        return -1;
    memcpy(entry, termios_p, sizeof(struct termios));
    return 0;
}
