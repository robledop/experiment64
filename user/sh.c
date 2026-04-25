#include <fcntl.h>
#include <path.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termcolors.h>
#include <unistd.h>

#define MAX_COMMAND_LENGTH 256
#define COMMAND_HISTORY_SIZE 10
#define COMMAND_HISTORY_ENTRY_SIZE 256
#define SHELL_CTRL_C 0x03

#define MAXARGS 10

typedef enum {
    CMD_EXEC = 1,
    CMD_REDIR,
    CMD_PIPE,
    CMD_LIST,
    CMD_BACK,
} command_type_t;

struct cmd {
    command_type_t type;
};

struct execcmd {
    command_type_t type;
    char *argv[MAXARGS];
    char *eargv[MAXARGS];
};

struct redircmd {
    command_type_t type;
    struct cmd *cmd;
    char *file;
    char *efile;
    int mode;
    int fd;
};

struct pipecmd {
    command_type_t type;
    struct cmd *left;
    struct cmd *right;
};

struct listcmd {
    command_type_t type;
    struct cmd *left;
    struct cmd *right;
};

struct backcmd {
    command_type_t type;
    struct cmd *cmd;
};

static char g_command_history[COMMAND_HISTORY_SIZE][COMMAND_HISTORY_ENTRY_SIZE];
static int g_history_count = 0;

static volatile sig_atomic_t g_winch_pending = 0;

static void shell_sigint_handler(const int sig)
{
    (void)sig;
}

static void shell_sigchld_handler([[maybe_unused]] const int sig)
{
}

static void shell_sigwinch_handler(const int sig)
{
    (void)sig;
    g_winch_pending = 1;
}

static int fork_or_panic(void);
static struct cmd *shell_parse_cmd(char *);
[[noreturn]] static void shell_exec_cmd(struct cmd *cmd);

static void shell_exec_program(struct execcmd *cmd)
{
    execve(cmd->argv[0], cmd->argv, nullptr);

    if (cmd->argv[0][0] != '/' && !strchr(cmd->argv[0], '/')) {
        char bin_path[256];
        path_safe_copy(bin_path, sizeof(bin_path), "/bin/");
        size_t idx = strlen(bin_path);
        for (size_t i = 0; cmd->argv[0][i] && idx + 1 < sizeof(bin_path); i++)
            bin_path[idx++] = cmd->argv[0][i];
        bin_path[idx] = '\0';
        cmd->argv[0]  = bin_path;
        execve(bin_path, cmd->argv, nullptr);
    }

    printf("exec %s failed\n", cmd->argv[0]);
}

[[noreturn]] static void shell_exec_cmd(struct cmd *cmd)
{
    int p[2];
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if (cmd == nullptr) {
        exit();
        __builtin_unreachable();
    }

    switch (cmd->type) {
    default:
        panic("shell_exec_cmd");

    case CMD_EXEC:
        ecmd = (struct execcmd *)cmd;
        if (ecmd->argv[0] == nullptr)
            exit();
        shell_exec_program(ecmd);
        break;

    case CMD_REDIR:
        rcmd = (struct redircmd *)cmd;
        close(rcmd->fd);
        if (open(rcmd->file, rcmd->mode) < 0) {
            printf("open %s failed\n", rcmd->file);
            exit();
        }
        shell_exec_cmd(rcmd->cmd);
        break;

    case CMD_LIST:
        lcmd = (struct listcmd *)cmd;
        if (fork_or_panic() == 0)
            shell_exec_cmd(lcmd->left);
        wait(nullptr);
        shell_exec_cmd(lcmd->right);
        break;

    case CMD_PIPE:
        pcmd = (struct pipecmd *)cmd;
        if (pipe(p) < 0)
            panic("pipe");
        if (fork_or_panic() == 0) {
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            shell_exec_cmd(pcmd->left);
        }
        if (fork_or_panic() == 0) {
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            shell_exec_cmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);
        wait(nullptr);
        wait(nullptr);
        break;

    case CMD_BACK:
        bcmd = (struct backcmd *)cmd;
        if (fork_or_panic() == 0)
            shell_exec_cmd(bcmd->cmd);
        break;
    }
    exit();
    __builtin_unreachable();
}

typedef enum {
    SHELL_KEY_UP = 256,
    SHELL_KEY_DOWN,
    SHELL_KEY_RIGHT,
    SHELL_KEY_LEFT,
} shell_key_t;

static int read_key(void)
{
    int c = getchar_blocking();
    if (c == '\x1b') {
        int c2 = getchar_blocking();
        if (c2 == '[') {
            int c3 = getchar_blocking();
            switch (c3) {
            case 'A':
                return SHELL_KEY_UP;
            case 'B':
                return SHELL_KEY_DOWN;
            case 'C':
                return SHELL_KEY_RIGHT;
            case 'D':
                return SHELL_KEY_LEFT;
            default:
                break;
            }
        }
        return '\x1b';
    }
    return c;
}

static void shell_terminal_readline(char *out, const int max, const bool output_while_typing)
{
    int current_history_index = g_history_count;
    int i                     = 0;
    for (; i < max - 1; i++) {
        const int key = read_key();

        if (g_winch_pending && output_while_typing) {
            g_winch_pending = 0;
            char cwd[256];
            getcwd(cwd, sizeof(cwd));
            printf("\r\x1b[2K%s" KGRN "> " KWHT, cwd);
            for (int j = 0; j < i; j++)
                putchar(out[j]);
        }

        if (key == 0) {
            continue;
        }

        if (key == SHELL_CTRL_C) {
            if (output_while_typing)
                printf("^C\n");
            out[0] = '\0';
            return;
        }

        if (key == SHELL_KEY_UP) {
            if (current_history_index == 0) {
                i--;
                continue;
            }

            for (int j = 0; j < i; j++) {
                printf("\b \b");
            }
            current_history_index--;
            strncpy((char *)out, g_command_history[current_history_index], (uint32_t)max);
            i = (int)strnlen((char *)out, max) - 1;
            printf("%s", (char *)out);
            continue;
        }

        if (key == SHELL_KEY_DOWN) {
            for (int j = 0; j < i; j++) {
                printf("\b \b");
            }

            if (current_history_index >= g_history_count - 1) {
                current_history_index = g_history_count;
                out[0]                = '\0';
                i                     = -1;
                continue;
            }

            current_history_index++;
            strncpy((char *)out, g_command_history[current_history_index], (uint32_t)max);
            i = (int)strlen((char *)out) - 1;
            printf("%s", (char *)out);
            continue;
        }

        if (key == SHELL_KEY_LEFT) {
            if (i <= 0) {
                i = -1;
                continue;
            } else {
                printf("\x1b[D");
                i -= 2;
                continue;
            }
        }

        if (key == SHELL_KEY_RIGHT) {
            i--;
            continue;
        }

        if (key == '\n' || key == '\r') {
            putchar('\n');
            break;
        }

        if (key == '\b' && i <= 0) {
            i = -1;
            continue;
        }

        if (output_while_typing) {
            putchar((char)key);
        }

        if (key == '\b' && i > 0) {
            i -= 2;
            continue;
        }

        out[i] = (char)key;
    }

    out[i] = 0x00;
}

static void shell_render_prompt(void)
{
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    printf("%s" KGRN "> " KWHT, cwd);
}

static void shell_append_history(const char *line)
{
    if (strlen(line) == 0)
        return;

    const uint32_t copy_len = sizeof(g_command_history[0]);

    if (g_history_count == COMMAND_HISTORY_SIZE) {
        memmove(g_command_history[0], g_command_history[1], (COMMAND_HISTORY_SIZE - 1) * sizeof(g_command_history[0]));
        g_history_count = COMMAND_HISTORY_SIZE - 1;
    }

    strncpy(g_command_history[g_history_count], line, (int)copy_len);
    g_command_history[g_history_count][copy_len - 1] = '\0';
    g_history_count++;
}

static bool shell_is_exact_command(const char *command, const char *input)
{
    const uint32_t input_len = strnlen(input, MAX_COMMAND_LENGTH);
    return strncmp(command, input, strlen(command)) == 0 && input_len == strlen(command);
}

static bool shell_run_builtin(char *buf)
{
    if (starts_with("cd ", buf)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
        if (chdir(buf + 3) < 0)
            printf("cannot cd %s\n", buf + 3);
        return true;
    }

    if (shell_is_exact_command("exit", buf))
        exit();

    if (shell_is_exact_command("cls", buf)) {
        printf("\033[2J\033[H");
        return true;
    }

    if (shell_is_exact_command("reboot", buf)) {
        reboot();
        return false;
    }

    if (shell_is_exact_command("shutdown", buf)) {
        shutdown();
        return true;
    }

    return false;
}

static void shell_give_terminal_to(int pid)
{
    ioctl(STDIN_FILENO, TIOCSPGRP, &pid);
}

int main(void)
{
    int fd;

    while ((fd = open("/dev/console", O_RDWR)) >= 0) {
        if (fd >= 3) {
            close(fd);
            break;
        }
    }

    printf(KWHT "User mode shell started. Press CTRL + P to list active threads.\n");
    signal(SIGINT, shell_sigint_handler);
    signal(SIGCHLD, shell_sigchld_handler);
    signal(SIGWINCH, shell_sigwinch_handler);

    while (true) {
        int self_pid = getpid();
        shell_give_terminal_to(self_pid);
        shell_render_prompt();

        char buf[MAX_COMMAND_LENGTH] = {0};
        shell_terminal_readline(buf, sizeof(buf), true);

        shell_append_history(buf);
        if (shell_run_builtin(buf))
            continue;

        const int pid = fork_or_panic();
        if (pid == 0) {
            shell_exec_cmd(shell_parse_cmd(buf));
        }
        shell_give_terminal_to(pid);
        wait(nullptr);
        self_pid = getpid();
        shell_give_terminal_to(self_pid);
    }
}

static int fork_or_panic(void)
{
    int pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

static struct cmd *cmd_new_exec(void)
{
    struct execcmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CMD_EXEC;
    return (struct cmd *)cmd;
}

static struct cmd *cmd_new_redir(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
    struct redircmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type  = CMD_REDIR;
    cmd->cmd   = subcmd;
    cmd->file  = file;
    cmd->efile = efile;
    cmd->mode  = mode;
    cmd->fd    = fd;
    return (struct cmd *)cmd;
}

static struct cmd *cmd_new_pipe(struct cmd *left, struct cmd *right)
{
    struct pipecmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type  = CMD_PIPE;
    cmd->left  = left;
    cmd->right = right;
    return (struct cmd *)cmd;
}

static struct cmd *cmd_new_list(struct cmd *left, struct cmd *right)
{
    struct listcmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type  = CMD_LIST;
    cmd->left  = left;
    cmd->right = right;
    return (struct cmd *)cmd;
}

static struct cmd *cmd_new_background(struct cmd *subcmd)
{
    struct backcmd *cmd = malloc(sizeof(*cmd));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CMD_BACK;
    cmd->cmd  = subcmd;
    return (struct cmd *)cmd;
}

static const char shell_whitespace[] = " \t\r\n\v";
static const char shell_symbols[]    = "<|>&;()";

static int gettoken(char **ps, const char *es, char **q, char **eq)
{
    char *s = *ps;
    while (s < es && strchr(shell_whitespace, *s))
        s++;
    if (q)
        *q = s;
    int ret = (unsigned char)*s;
    switch (*s) {
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
        if (*s == '>') {
            ret = '+';
            s++;
        }
        break;
    default:
        ret = 'a';
        while (s < es && !strchr(shell_whitespace, *s) && !strchr(shell_symbols, *s))
            s++;
        break;
    }
    if (eq)
        *eq = s;

    while (s < es && strchr(shell_whitespace, *s))
        s++;
    *ps = s;
    return ret;
}

static int peek(char **ps, const char *es, const char *toks)
{
    char *s = *ps;
    while (s < es && strchr(shell_whitespace, *s))
        s++;
    *ps = s;
    return *s && strchr(toks, *s);
}

static struct cmd *parse_line(char **, char *);
static struct cmd *parse_pipe(char **, char *);
static struct cmd *parse_exec(char **, char *);
static struct cmd *nulterminate(struct cmd *);

static struct cmd *shell_parse_cmd(char *s)
{
    char *es        = s + strlen(s);
    struct cmd *cmd = parse_line(&s, es);
    peek(&s, es, "");
    if (s != es) {
        printf("leftovers: %s\n", s);
        panic("syntax");
    }
    nulterminate(cmd);
    return cmd;
}

static struct cmd *parse_line(char **ps, char *es)
{
    struct cmd *cmd = parse_pipe(ps, es);
    while (peek(ps, es, "&")) {
        gettoken(ps, es, nullptr, nullptr);
        cmd = cmd_new_background(cmd);
    }
    if (peek(ps, es, ";")) {
        gettoken(ps, es, nullptr, nullptr);
        cmd = cmd_new_list(cmd, parse_line(ps, es));
    }
    return cmd;
}

static struct cmd *parse_pipe(char **ps, char *es)
{
    struct cmd *cmd = parse_exec(ps, es);
    if (peek(ps, es, "|")) {
        gettoken(ps, es, nullptr, nullptr);
        cmd = cmd_new_pipe(cmd, parse_pipe(ps, es));
    }
    return cmd;
}

static struct cmd *parse_redirs(struct cmd *cmd, char **ps, char *es)
{
    char *q, *eq;

    while (peek(ps, es, "<>")) {
        int tok = gettoken(ps, es, nullptr, nullptr);
        if (gettoken(ps, es, &q, &eq) != 'a')
            panic("missing file for redirection");
        switch (tok) {
        case '<':
            cmd = cmd_new_redir(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>':
            cmd = cmd_new_redir(cmd, q, eq, O_WRONLY | O_CREAT, 1);
            break;
        case '+': // >>
            cmd = cmd_new_redir(cmd, q, eq, O_WRONLY | O_CREAT | O_APPEND, 1);
            break;
        default:
            break;
        }
    }
    return cmd;
}

static struct cmd *parse_block(char **ps, char *es)
{
    if (!peek(ps, es, "("))
        panic("parse_block");
    gettoken(ps, es, nullptr, nullptr);
    struct cmd *cmd = parse_line(ps, es);
    if (!peek(ps, es, ")"))
        panic("syntax - missing )");
    gettoken(ps, es, nullptr, nullptr);
    cmd = parse_redirs(cmd, ps, es);
    return cmd;
}

static struct cmd *parse_exec(char **ps, char *es)
{
    char *q, *eq;
    int tok;

    if (peek(ps, es, "("))
        return parse_block(ps, es);

    struct cmd *ret     = cmd_new_exec();
    struct execcmd *cmd = (struct execcmd *)ret;

    int argc = 0;
    ret      = parse_redirs(ret, ps, es);
    while (!peek(ps, es, "|)&;")) {
        if ((tok = gettoken(ps, es, &q, &eq)) == 0)
            break;
        if (tok != 'a')
            panic("syntax");
        cmd->argv[argc]  = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS)
            panic("too many args");
        ret = parse_redirs(ret, ps, es);
    }
    cmd->argv[argc]  = nullptr;
    cmd->eargv[argc] = nullptr;
    return ret;
}

static struct cmd *nulterminate(struct cmd *cmd)
{
    int i;
    struct backcmd *bcmd;
    struct execcmd *ecmd;
    struct listcmd *lcmd;
    struct pipecmd *pcmd;
    struct redircmd *rcmd;

    if (cmd == nullptr)
        return nullptr;

    switch (cmd->type) {
    case CMD_EXEC:
        ecmd = (struct execcmd *)cmd;
        for (i = 0; ecmd->argv[i]; i++)
            *ecmd->eargv[i] = 0;
        break;

    case CMD_REDIR:
        rcmd = (struct redircmd *)cmd;
        nulterminate(rcmd->cmd);
        *rcmd->efile = 0;
        break;

    case CMD_PIPE:
        pcmd = (struct pipecmd *)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;

    case CMD_LIST:
        lcmd = (struct listcmd *)cmd;
        nulterminate(lcmd->left);
        nulterminate(lcmd->right);
        break;

    case CMD_BACK:
        bcmd = (struct backcmd *)cmd;
        nulterminate(bcmd->cmd);
        break;

    default:
        break;
    }
    return cmd;
}
