#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

#define SHELL_PATH_MAX 256
#define SHELL_MAX_SEGMENTS 64

static char cwd[SHELL_PATH_MAX] = "/";

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i])
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void simplify_path(char *path)
{
    char buffer[SHELL_PATH_MAX];
    safe_copy(buffer, sizeof(buffer), path);

    char *segments[SHELL_MAX_SEGMENTS];
    int seg_count = 0;
    char *p = buffer;

    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        char *segment = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = '\0';

        if (strcmp(segment, ".") == 0)
            continue;
        if (strcmp(segment, "..") == 0)
        {
            if (seg_count > 0)
                seg_count--;
            continue;
        }
        if (seg_count < SHELL_MAX_SEGMENTS)
            segments[seg_count++] = segment;
    }

    size_t idx = 0;
    path[idx++] = '/';
    for (int i = 0; i < seg_count && idx < SHELL_PATH_MAX - 1; i++)
    {
        const char *seg = segments[i];
        for (size_t j = 0; seg[j] && idx < SHELL_PATH_MAX - 1; j++)
            path[idx++] = seg[j];
        if (i != seg_count - 1 && idx < SHELL_PATH_MAX - 1)
            path[idx++] = '/';
    }
    if (idx == 1)
        path[1] = '\0';
    else
        path[idx] = '\0';
}

static void build_absolute_path(const char *input, char *output, size_t size)
{
    if (!output || size == 0)
        return;

    if (!input || !*input)
    {
        safe_copy(output, size, cwd);
        return;
    }

    if (*input == '/')
    {
        safe_copy(output, size, input);
    }
    else
    {
        size_t idx = 0;
        if (cwd[0] == '/' && cwd[1] == '\0')
        {
            output[idx++] = '/';
        }
        else
        {
            for (size_t i = 0; cwd[i] && idx + 1 < size; i++)
                output[idx++] = cwd[i];
        }

        if (idx > 1 && output[idx - 1] != '/' && idx + 1 < size)
            output[idx++] = '/';

        for (size_t i = 0; input[i] && idx + 1 < size; i++)
            output[idx++] = input[i];
        output[idx] = '\0';
    }

    simplify_path(output);
}

static int path_exists(const char *path)
{
    if (!path || !*path)
        return 0;

    int fd = open(path);
    if (fd < 0)
        return 0;

    close(fd);
    return 1;
}

static int resolve_command_path(const char *command, char *resolved, size_t size)
{
    if (!command || !*command)
        return -1;

    if (command[0] == '/')
    {
        safe_copy(resolved, size, command);
        return 0;
    }

    char abs_path[SHELL_PATH_MAX];
    build_absolute_path(command, abs_path, sizeof(abs_path));
    if (path_exists(abs_path))
    {
        safe_copy(resolved, size, abs_path);
        return 0;
    }

    if (!strchr(command, '/'))
    {
        size_t cmd_len = strlen(command);
        if (cmd_len + 5 < SHELL_PATH_MAX)
        {
            char bin_path[SHELL_PATH_MAX];
            safe_copy(bin_path, sizeof(bin_path), "/bin/");
            size_t idx = strlen(bin_path);
            for (size_t i = 0; command[i] && idx + 1 < SHELL_PATH_MAX; i++)
                bin_path[idx++] = command[i];
            bin_path[idx] = '\0';

            if (path_exists(bin_path))
            {
                safe_copy(resolved, size, bin_path);
                return 0;
            }
        }
    }

    return -1;
}

static void shell_print_prompt(void)
{
    printf("%s$ ", cwd);
}

static int shell_change_directory(const char *path)
{
    char resolved[SHELL_PATH_MAX];
    const char *target = (path && *path) ? path : "/";
    build_absolute_path(target, resolved, sizeof(resolved));

    if (chdir(resolved) != 0)
        return -1;

    safe_copy(cwd, sizeof(cwd), resolved);
    return 0;
}

static int shell_run_command(const char *command)
{
    char resolved[SHELL_PATH_MAX];
    if (resolve_command_path(command, resolved, sizeof(resolved)) != 0)
        return -1;

    int pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0)
    {
        exec(resolved);
        printf("Failed to exec %s\n", resolved);
        exit(1);
    }

    int status = 0;
    if (wait(&status) < 0)
        return -1;

    return status;
}

int main(void)
{
    char buf[128];
    printf("User mode shell started\n");

    while (1)
    {
        memset(buf, 0, sizeof(buf));
        shell_print_prompt();

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
                buf[i++] = (char)c;
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
            printf("Commands: help, exit, clear, cd, sleep, colors, cursor, reset, test_ansi, test_sbrk, test_malloc\n");
        }
        else if (strncmp(buf, "cd", 2) == 0 && (buf[2] == '\0' || buf[2] == ' '))
        {
            const char *arg = buf + 2;
            while (*arg == ' ')
                arg++;

            const char *target = (*arg == '\0') ? "/" : arg;
            if (shell_change_directory(target) != 0)
            {
                printf("cd: no such directory: %s\n", target);
            }
        }
        else if (strncmp(buf, "sleep", 5) == 0 && (buf[5] == '\0' || buf[5] == ' '))
        {
            const char *arg = buf + 5;
            while (*arg == ' ')
                arg++;

            char *endptr = nullptr;
            long parsed = (*arg) ? strtol(arg, &endptr, 10) : 0;
            if (endptr == arg || parsed < 0)
                parsed = 0;
            int duration = (parsed > INT_MAX) ? INT_MAX : (int)parsed;
            if (duration < 0)
                duration = 0;
            sleep(duration);
        }
        else if (strcmp(buf, "clear") == 0)
        {
            printf("\033[2J\033[H");
        }
        else if (strcmp(buf, "colors") == 0)
        {
            for (int i = 30; i <= 37; i++)
            {
                printf("\033[%dmColor %d\033[0m\n", i, i);
            }
            printf("\nBold:\n");
            for (int i = 30; i <= 37; i++)
            {
                printf("\033[1;%dmColor %d\033[0m\n", i, i);
            }
            printf("\nBackgrounds:\n");
            for (int i = 40; i <= 47; i++)
            {
                printf("\033[%dmColor %d\033[0m\n", i, i);
            }
        }
        else if (strcmp(buf, "cursor") == 0)
        {
            printf("\033[2J\033[H"); // Clear and home
            printf("Top Left\n");
            printf("\033[10B\033[10C"); // Down 10, Right 10
            printf("Middle");
            printf("\033[5A"); // Up 5
            printf(" Up 5");
            printf("\033[5D"); // Left 5
            printf(" Left 5");
            printf("\033[H"); // Home
        }
        else if (strcmp(buf, "test_ansi") == 0)
        {
            printf("Line 1\n");
            printf("Line 2 to be cleared partially...");
            printf("\033[10D"); // Move back 10
            printf("\033[0K");  // Clear to end of line
            printf("CLEARED\n");

            printf("Line 3\n");
            printf("Line 4\n");
            printf("\033[2A"); // Up 2
            printf("\033[0J"); // Clear below
            printf("Cleared below this line.\n");
        }
        else if (strcmp(buf, "reset") == 0)
        {
            printf("\033c");
        }
        else if (strcmp(buf, "test_sbrk") == 0)
        {
            void *p1 = sbrk(0);
            printf("Current break: %p\n", p1);
            void *p2 = sbrk(4096);
            printf("Allocated 4096 bytes. Old break: %p\n", p2);
            void *p3 = sbrk(0);
            printf("New break: %p\n", p3);
            if ((char *)p3 - (char *)p2 == 4096)
                printf("Sbrk seems to work!\n");
            else
                printf("Sbrk failed!\n");

            // Write to new memory
            int *arr = (int *)p2;
            arr[0] = 123;
            printf("Wrote 123 to new memory: %d\n", arr[0]);
        }
        else if (strcmp(buf, "test_malloc") == 0)
        {
            printf("Testing malloc...\n");
            int *ptr = (int *)malloc(sizeof(int) * 10);
            if (!ptr)
            {
                printf("malloc failed\n");
            }
            else
            {
                printf("malloc succeeded: %p\n", ptr);
                for (int i = 0; i < 10; i++)
                    ptr[i] = i;
                printf("Data written. Reading back:\n");
                for (int i = 0; i < 10; i++)
                    printf("%d ", ptr[i]);
                printf("\n");

                printf("Freeing memory...\n");
                free(ptr);
                printf("Memory freed.\n");

                printf("Allocating again (should reuse block if implemented)...\n");
                int *ptr2 = (int *)malloc(sizeof(int) * 10);
                printf("malloc succeeded: %p\n", ptr2);
                if (ptr == ptr2)
                    printf("Block reused!\n");
                else
                    printf("Block not reused (new address).\n");
                free(ptr2);
            }
        }
        else
        {
            if (shell_run_command(buf) < 0)
            {
                printf("Command not found or exec failed: %s\n", buf);
            }
        }
    }
    return 0;
}
