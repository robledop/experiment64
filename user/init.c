#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

[[noreturn]] int main(void)
{
    printf("Init process started (PID %d)\n", getpid());

    printf("Starting httpd...\n");
    const int pid_httpd = fork();
    if (pid_httpd == 0) {
        exec("/bin/httpd");
        printf("Failed to exec httpd\n");
        exit(1);
    }

#ifndef HEADLESS
    printf("Starting window manager...\n");
    const int pid_wm = fork();
    if (pid_wm == 0) {
        exec("/bin/wm");
        printf("Failed to exec wm\n");
        exit(1);
    } else if (pid_wm > 0) {
        int status;
        for (;;) {
            const int pid = wait(&status);
            if (pid < 0 || pid == pid_wm)
                break;
        }
        printf("Window manager exited with status %d\n", status);
    }
#endif

    while (1) {
        printf("Starting shell...\n");
        const int pid_sh = fork();
        if (pid_sh == 0) {
            exec("/bin/sh");
            printf("Failed to exec shell\n");
            exit(1);
        } else if (pid_sh > 0) {
            // Only restart the shell, not other child processes, but wait() all child processes to reap them.
            int status;
            for (;;) {
                const int pid = wait(&status);
                if (pid < 0)
                    break;
                if (pid == pid_sh) {
                    printf("Shell exited with status %d\n", status);
                    break;
                }
            }
        } else {
            printf("Failed to fork\n");
        }
    }
}