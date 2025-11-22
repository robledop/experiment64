#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char buf[128];
    printf("Welcome to Minimal Shell!\n");

    while (1)
    {
        printf("$ ");

        // Simple gets implementation since the one in stdio might be too basic
        // or we want to handle backspace here if the kernel doesn't do canonical mode
        int i = 0;
        while (1)
        {
            int c = getchar();
            if (c == EOF)
                return 0;
            if (c == '\n')
            {
                putchar('\n');
                buf[i] = '\0';
                break;
            }
            if (c == '\b' || c == 127)
            { // Backspace
                if (i > 0)
                {
                    i--;
                    printf("\b \b"); // Erase character on screen
                }
            }
            else if (i < 127)
            {
                buf[i++] = c;
                putchar(c); // Echo
            }
        }

        if (strlen(buf) == 0)
            continue;

        if (strcmp(buf, "exit") == 0)
        {
            break;
        }
        else if (strcmp(buf, "help") == 0)
        {
            printf("Commands: help, exit, clear\n");
        }
        else if (strcmp(buf, "clear") == 0)
        {
            printf("\033[2J\033[H");
        }
        else
        {
            int pid = spawn(buf);
            if (pid > 0)
            {
                int status;
                wait(&status);
            }
            else
            {
                printf("Command not found or spawn failed: %s\n", buf);
            }
        }
    }
    return 0;
}
