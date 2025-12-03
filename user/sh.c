#include <stdio.h>
#include <fcntl.h>
#include <termcolors.h>
#include <unistd.h>
#include <string.h>
#include <path.h>

#define MAX_COMMAND_LENGTH 256
#define COMMAND_HISTORY_SIZE 10
#define COMMAND_HISTORY_ENTRY_SIZE 256

// Parsed command representation
#define EXEC 1
#define REDIR 2
#define PIPE 3
#define LIST 4
#define BACK 5

#define MAXARGS 10

struct cmd
{
    int type;
};

struct execcmd
{
    int type;
    char *argv[MAXARGS];
    char *eargv[MAXARGS];
};

struct redircmd
{
    int type;
    struct cmd *cmd;
    char *file;
    char *efile;
    int mode;
    int fd;
};

struct pipecmd
{
    int type;
    struct cmd *left;
    struct cmd *right;
};

struct listcmd
{
    int type;
    struct cmd *left;
    struct cmd *right;
};

struct backcmd
{
    int type;
    struct cmd *cmd;
};

static char command_history[COMMAND_HISTORY_SIZE][COMMAND_HISTORY_ENTRY_SIZE];
static int history_count = 0;

int fork1(void); // Fork but panics on failure.
struct cmd *parsecmd(char *);

// Execute cmd.  Never returns.
[[noreturn]] void runcmd(struct cmd *cmd)
{
    int p[2];
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if (cmd == nullptr)
    {
        exit();
        __builtin_unreachable();
    }

    switch (cmd->type)
    {
    default:
        panic("runcmd");

    case EXEC:
        ecmd = (struct execcmd *)cmd;
        if (ecmd->argv[0] == nullptr)
            exit();
        // Try the command as given first
        execve(ecmd->argv[0], ecmd->argv, nullptr);
        // If that failed and it's not an absolute/relative path, try /bin/
        if (ecmd->argv[0][0] != '/' && !strchr(ecmd->argv[0], '/'))
        {
            char bin_path[256];
            path_safe_copy(bin_path, sizeof(bin_path), "/bin/");
            size_t idx = strlen(bin_path);
            for (size_t i = 0; ecmd->argv[0][i] && idx + 1 < sizeof(bin_path); i++)
                bin_path[idx++] = ecmd->argv[0][i];
            bin_path[idx] = '\0';
            ecmd->argv[0] = bin_path;
            execve(bin_path, ecmd->argv, nullptr);
        }
        printf("exec %s failed\n", ecmd->argv[0]);
        break;

    case REDIR:
        rcmd = (struct redircmd *)cmd;
        close(rcmd->fd);
        if (open(rcmd->file, rcmd->mode) < 0)
        {
            printf("open %s failed\n", rcmd->file);
            exit();
        }
        runcmd(rcmd->cmd);
        break;

    case LIST:
        lcmd = (struct listcmd *)cmd;
        if (fork1() == 0)
            runcmd(lcmd->left);
        wait(nullptr);
        runcmd(lcmd->right);
        break;

    case PIPE:
        pcmd = (struct pipecmd *)cmd;
        if (pipe(p) < 0)
            panic("pipe");
        if (fork1() == 0)
        {
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            runcmd(pcmd->left);
        }
        if (fork1() == 0)
        {
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            runcmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);
        wait(nullptr);
        wait(nullptr);
        break;

    case BACK:
        bcmd = (struct backcmd *)cmd;
        if (fork1() == 0)
            runcmd(bcmd->cmd);
        break;
    }
    exit();
    __builtin_unreachable();
}

// Special key codes for internal use
#define KEY_UP 256
#define KEY_DOWN 257
#define KEY_RIGHT 258
#define KEY_LEFT 259

// Read a key, handling ANSI escape sequences for arrow keys
static int read_key(void)
{
    int c = getchar_blocking();
    if (c == '\x1b')
    {
        // Escape sequence - try to read more
        int c2 = getchar_blocking();
        if (c2 == '[')
        {
            int c3 = getchar_blocking();
            switch (c3)
            {
            case 'A':
                return KEY_UP;
            case 'B':
                return KEY_DOWN;
            case 'C':
                return KEY_RIGHT;
            case 'D':
                return KEY_LEFT;
            default:
                break;
            }
        }
        // Not a recognized sequence, return ESC
        return '\x1b';
    }
    return c;
}

void shell_terminal_readline(char *out, const int max, const bool output_while_typing)
{
    int current_history_index = history_count;
    int i = 0;
    for (; i < max - 1; i++)
    {
        const int key = read_key();
        if (key == 0)
        {
            continue;
        }

        // Up arrow
        if (key == KEY_UP)
        {
            if (current_history_index == 0)
            {
                i--;
                continue;
            }

            // Clear current line
            for (int j = 0; j < i; j++)
            {
                printf("\b \b");
            }
            current_history_index--;
            strncpy((char *)out, command_history[current_history_index], (uint32_t)max);
            i = (int)strnlen((char *)out, max) - 1;
            printf("%s", (char *)out);
            continue;
        }

        // Down arrow
        if (key == KEY_DOWN)
        {
            // Clear current line
            for (int j = 0; j < i; j++)
            {
                printf("\b \b");
            }

            if (current_history_index >= history_count - 1)
            {
                // At the end of history, show empty line
                current_history_index = history_count;
                out[0] = '\0';
                i = -1;
                continue;
            }

            current_history_index++;
            strncpy((char *)out, command_history[current_history_index], (uint32_t)max);
            i = (int)strlen((char *)out) - 1;
            printf("%s", (char *)out);
            continue;
        }

        // Left arrow key
        if (key == KEY_LEFT)
        {
            if (i <= 0)
            {
                i = -1;
                continue;
            }
            else
            {
                printf("\x1b[D"); // Move cursor left
                i -= 2;
                continue;
            }
        }

        // Right arrow key
        if (key == KEY_RIGHT)
        {
            // For now, just decrement to counteract the loop increment
            i--;
            continue;
        }

        if (key == '\n' || key == '\r')
        {
            putchar('\n'); // Echo the newline before executing command
            break;
        }

        if (key == '\b' && i <= 0)
        {
            i = -1;
            continue;
        }

        if (output_while_typing)
        {
            putchar((char)key);
        }

        if (key == '\b' && i > 0)
        {
            i -= 2;
            continue;
        }

        out[i] = (char)key;
    }

    out[i] = 0x00;
}

int main(void)
{
    int fd;
    int child_pid = -1;

    // Ensure that three file descriptors are open.
    while ((fd = open("/dev/console", O_RDWR)) >= 0)
    {
        if (fd >= 3)
        {
            close(fd);
            break;
        }
    }

    printf(KWHT "User mode shell started\n");

    // Read and run input commands.
    while (true)
    {
        char cwd[256];
        getcwd(cwd, sizeof(cwd));
        printf("%s" KGRN "> " KWHT, cwd);

        char buf[MAX_COMMAND_LENGTH] = {0};
        shell_terminal_readline(buf, sizeof(buf), true);

        if (strlen((char *)buf) != 0)
        {
            uint32_t copy_len = sizeof(command_history[0]);

            if (history_count == COMMAND_HISTORY_SIZE)
            {
                memmove(command_history[0],
                        command_history[1],
                        (COMMAND_HISTORY_SIZE - 1) * sizeof(command_history[0]));
                history_count = COMMAND_HISTORY_SIZE - 1;
            }

            strncpy(command_history[history_count], (char *)buf, (int)copy_len);
            command_history[history_count][copy_len - 1] = '\0';
            history_count++;
        }

        if (starts_with("cd ", buf))
        {
            // Chdir must be called by the parent, not the child.
            buf[strlen(buf)] = 0;
            if (chdir(buf + 3) < 0)
                printf("cannot cd %s\n", buf + 3);
            continue;
        }

        const uint32_t input_len = strnlen(buf, 100);
        if (strncmp("exit", buf, 4) == 0 && input_len == 4)
        {
            exit();
        }

        if (strncmp("cls", buf, 3) == 0 && input_len == 3)
        {
            // Clear the screen
            printf("\033[2J\033[H");
            continue;
        }

        if (strncmp("reboot", buf, 6) == 0 && input_len == 6)
        {
            reboot();
        }

        if (strncmp("shutdown", buf, 8) == 0 && input_len == 8)
        {
            shutdown();
            continue;
        }

        bool return_immediately = false;
        return_immediately = ends_with((char *)buf, " &");

        if (return_immediately)
        {
            buf[strlen((char *)buf) - 2] = 0x00;
        }

        child_pid = fork1();
        if (child_pid == 0)
        {
            runcmd(parsecmd(buf));
        }
        if (!return_immediately)
        {
            wait(nullptr);
        }
    }
}

int fork1(void)
{
    int pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

// Constructors

struct cmd *execcmd(void)
{
    struct execcmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;
    return (struct cmd *)cmd;
}

struct cmd *redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
    struct redircmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDIR;
    cmd->cmd = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->mode = mode;
    cmd->fd = fd;
    return (struct cmd *)cmd;
}

struct cmd *pipecmd(struct cmd *left, struct cmd *right)
{
    struct pipecmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd *)cmd;
}

struct cmd *listcmd(struct cmd *left, struct cmd *right)
{
    struct listcmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd *)cmd;
}

struct cmd *backcmd(struct cmd *subcmd)
{
    struct backcmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->cmd = subcmd;
    return (struct cmd *)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int gettoken(char **ps, const char *es, char **q, char **eq)
{
    char *s = *ps;
    while (s < es && strchr(whitespace, *s))
        s++;
    if (q)
        *q = s;
    int ret = (unsigned char)*s;
    switch (*s)
    {
    case 0:
        break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if (*s == '>')
        {
            ret = '+';
            s++;
        }
        break;
    default:
        ret = 'a';
        while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
            s++;
        break;
    }
    if (eq)
        *eq = s;

    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return ret;
}

int peek(char **ps, const char *es, char *toks)
{
    char *s = *ps;
    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *parsecmd(char *s)
{
    char *es = s + strlen(s);
    struct cmd *cmd = parseline(&s, es);
    peek(&s, es, "");
    if (s != es)
    {
        printf("leftovers: %s\n", s);
        panic("syntax");
    }
    nulterminate(cmd);
    return cmd;
}

struct cmd *parseline(char **ps, char *es)
{
    struct cmd *cmd = parsepipe(ps, es);
    while (peek(ps, es, "&"))
    {
        gettoken(ps, es, nullptr, nullptr);
        cmd = backcmd(cmd);
    }
    if (peek(ps, es, ";"))
    {
        gettoken(ps, es, nullptr, nullptr);
        cmd = listcmd(cmd, parseline(ps, es));
    }
    return cmd;
}

struct cmd *parsepipe(char **ps, char *es)
{
    struct cmd *cmd = parseexec(ps, es);
    if (peek(ps, es, "|"))
    {
        gettoken(ps, es, nullptr, nullptr);
        cmd = pipecmd(cmd, parsepipe(ps, es));
    }
    return cmd;
}

struct cmd *parseredirs(struct cmd *cmd, char **ps, char *es)
{
    char *q, *eq;

    while (peek(ps, es, "<>"))
    {
        int tok = gettoken(ps, es, nullptr, nullptr);
        if (gettoken(ps, es, &q, &eq) != 'a')
            panic("missing file for redirection");
        switch (tok)
        {
        case '<':
            cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>':  // NOLINT(bugprone-branch-clone) - intentional fallthrough for > and >>
        case '+': // >>
            cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
            break;
        default:
            break;
        }
    }
    return cmd;
}

struct cmd *parseblock(char **ps, char *es)
{
    if (!peek(ps, es, "("))
        panic("parseblock");
    gettoken(ps, es, nullptr, nullptr);
    struct cmd *cmd = parseline(ps, es);
    if (!peek(ps, es, ")"))
        panic("syntax - missing )");
    gettoken(ps, es, nullptr, nullptr);
    cmd = parseredirs(cmd, ps, es);
    return cmd;
}

struct cmd *parseexec(char **ps, char *es)
{
    char *q, *eq;
    int tok;

    if (peek(ps, es, "("))
        return parseblock(ps, es);

    struct cmd *ret = execcmd();
    struct execcmd *cmd = (struct execcmd *)ret;

    int argc = 0;
    ret = parseredirs(ret, ps, es);
    while (!peek(ps, es, "|)&;"))
    {
        if ((tok = gettoken(ps, es, &q, &eq)) == 0)
            break;
        if (tok != 'a')
            panic("syntax");
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS)
            panic("too many args");
        ret = parseredirs(ret, ps, es);
    }
    cmd->argv[argc] = nullptr;
    cmd->eargv[argc] = nullptr;
    return ret;
}

// NUL-terminate all the counted strings.
struct cmd *nulterminate(struct cmd *cmd)
{
    int i;
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if (cmd == nullptr)
        return nullptr;

    switch (cmd->type)
    {
    case EXEC:
        ecmd = (struct execcmd *)cmd;
        for (i = 0; ecmd->argv[i]; i++)
            *ecmd->eargv[i] = 0;
        break;

    case REDIR:
        rcmd = (struct redircmd *)cmd;
        nulterminate(rcmd->cmd);
        *rcmd->efile = 0;
        break;

    case PIPE:
        pcmd = (struct pipecmd *)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;

    case LIST:
        lcmd = (struct listcmd *)cmd;
        nulterminate(lcmd->left);
        nulterminate(lcmd->right);
        break;

    case BACK:
        bcmd = (struct backcmd *)cmd;
        nulterminate(bcmd->cmd);
        break;

    default:
        break;
    }
    return cmd;
}
