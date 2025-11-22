#include "test.h"
#include "fat32.h"
#include "string.h"
#include "terminal.h"
#include "heap.h"

extern fat32_fs_t fs;
extern bool fs_initialized;

static void itoa(int n, char *s)
{
    int i = 0;
    if (n == 0)
    {
        s[i++] = '0';
        s[i] = '\0';
        return;
    }
    while (n > 0)
    {
        s[i++] = (n % 10) + '0';
        n /= 10;
    }
    s[i] = '\0';
    // Reverse
    for (int j = 0; j < i / 2; j++)
    {
        char temp = s[j];
        s[j] = s[i - 1 - j];
        s[i - 1 - j] = temp;
    }
}

static void my_strcat(char *dest, const char *src)
{
    while (*dest)
        dest++;
    while (*src)
        *dest++ = *src++;
    *dest = 0;
}

TEST(test_fat32_stress)
{
    if (!fs_initialized)
        return false;

    const char *dirname = "STRESS";
    printf("Creating stress directory %s...\n", dirname);

    // Create directory
    if (fat32_create_dir(&fs, dirname) != 0)
    {
        // It might already exist from previous run, try to continue
        printf("Directory creation failed (might exist), continuing...\n");
    }

    int num_subdirs = 5;
    int files_per_subdir = 5;
    char path[64];
    char content[32];

    // 1. Create Subdirectories and Files
    for (int i = 0; i < num_subdirs; i++)
    {
        strcpy(path, dirname);
        my_strcat(path, "/DIR");
        char num[10];
        itoa(i, num);
        my_strcat(path, num);

        // printf("Creating directory %s...\n", path);
        if (fat32_create_dir(&fs, path) != 0)
        {
            printf("Failed to create directory %s\n", path);
            return false;
        }

        for (int j = 0; j < files_per_subdir; j++)
        {
            char filepath[64];
            strcpy(filepath, path);
            my_strcat(filepath, "/FILE");
            char fnum[10];
            itoa(j, fnum);
            my_strcat(filepath, fnum);
            my_strcat(filepath, ".TXT");

            strcpy(content, "Data ");
            my_strcat(content, num);
            my_strcat(content, "-");
            my_strcat(content, fnum);

            if (fat32_write_file(&fs, filepath, (uint8_t *)content, strlen(content)) != 0)
            {
                printf("Failed to write %s\n", filepath);
                return false;
            }
        }
    }
    printf("Created %d subdirectories with %d files each.\n", num_subdirs, files_per_subdir);

    // 2. Verify Files
    for (int i = 0; i < num_subdirs; i++)
    {
        strcpy(path, dirname);
        my_strcat(path, "/DIR");
        char num[10];
        itoa(i, num);
        my_strcat(path, num);

        for (int j = 0; j < files_per_subdir; j++)
        {
            char filepath[64];
            strcpy(filepath, path);
            my_strcat(filepath, "/FILE");
            char fnum[10];
            itoa(j, fnum);
            my_strcat(filepath, fnum);
            my_strcat(filepath, ".TXT");

            strcpy(content, "Data ");
            my_strcat(content, num);
            my_strcat(content, "-");
            my_strcat(content, fnum);

            uint8_t buffer[64];
            memset(buffer, 0, 64);
            if (fat32_read_file(&fs, filepath, buffer, 64) != 0)
            {
                printf("Failed to read %s\n", filepath);
                return false;
            }

            if (strncmp((char *)buffer, content, strlen(content)) != 0)
            {
                printf("Content mismatch for %s: expected '%s', got '%s'\n", filepath, content, buffer);
                return false;
            }
        }
    }
    printf("Verified all files.\n");

    // 3. Delete Files and Subdirectories
    for (int i = 0; i < num_subdirs; i++)
    {
        strcpy(path, dirname);
        my_strcat(path, "/DIR");
        char num[10];
        itoa(i, num);
        my_strcat(path, num);

        for (int j = 0; j < files_per_subdir; j++)
        {
            char filepath[64];
            strcpy(filepath, path);
            my_strcat(filepath, "/FILE");
            char fnum[10];
            itoa(j, fnum);
            my_strcat(filepath, fnum);
            my_strcat(filepath, ".TXT");

            if (fat32_delete_file(&fs, filepath) != 0)
            {
                printf("Failed to delete %s\n", filepath);
                return false;
            }
        }

        // Delete subdir
        if (fat32_delete_file(&fs, path) != 0)
        {
            printf("Failed to delete directory %s\n", path);
            return false;
        }
    }
    printf("Deleted all subdirectories and files.\n");

    // 4. Delete Root Stress Directory
    printf("Deleting root stress directory...\n");
    if (fat32_delete_file(&fs, dirname) != 0)
    {
        printf("Failed to delete root stress directory\n");
        return false;
    }

    printf("test_fat32_stress: Finished successfully.\n");
    return true;
}