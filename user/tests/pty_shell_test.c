#include <pty.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define CAPTURE_SIZE 4096

int main(void)
{
    int pty_fds[2] = {-1, -1};
    if (openpty(pty_fds) != 0)
        return 1;

    int master_fd = pty_fds[0];
    int slave_fd = pty_fds[1];

    int pid = fork();
    if (pid < 0) {
        close(master_fd);
        close(slave_fd);
        return 2;
    }

    if (pid == 0) {
        if (dup2(slave_fd, STDIN_FILENO) < 0)
            exit(120);
        if (dup2(slave_fd, STDOUT_FILENO) < 0)
            exit(121);
        if (dup2(slave_fd, STDERR_FILENO) < 0)
            exit(122);

        if (master_fd != STDIN_FILENO && master_fd != STDOUT_FILENO && master_fd != STDERR_FILENO)
            close(master_fd);
        if (slave_fd != STDIN_FILENO && slave_fd != STDOUT_FILENO && slave_fd != STDERR_FILENO)
            close(slave_fd);
        exec("/bin/sh");
        exit(127);
    }

    close(slave_fd);

    const char *script = "echo PTY_OK\nexit\n";
    if (write(master_fd, script, strlen(script)) != (ssize_t)strlen(script)) {
        close(master_fd);
        return 3;
    }

    int status = -1;
    if (waitpid(pid, &status, 0) != pid) {
        close(master_fd);
        return 4;
    }

    char capture[CAPTURE_SIZE];
    size_t used = 0;
    for (;;) {
        char chunk[256];
        ssize_t n = read(master_fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        size_t remaining = CAPTURE_SIZE - 1 - used;
        if (remaining == 0)
            continue;
        size_t copy_len = (size_t)n;
        if (copy_len > remaining)
            copy_len = remaining;
        memcpy(capture + used, chunk, copy_len);
        used += copy_len;
    }
    capture[used] = '\0';

    close(master_fd);

    if (status != 0)
        return 5;
    if (!strstr(capture, "PTY_OK"))
        return 6;

    return 0;
}
