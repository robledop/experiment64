#include <stdio.h>
#include <sys/fcntl.h>
#include <time.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }
    struct stat s;
    stat(argv[1], &s);
    printf("File: %s\n", argv[1]);
    printf("Size: %llu bytes\n", s.st_size);
    printf("Device: %d  Inode: %d   Links: %d\n", s.st_dev, s.st_ino, s.st_nlink);
    printf("Access: (");
    printf((s.st_mode & S_IRUSR) ? "r" : "-");
    printf((s.st_mode & S_IWUSR) ? "w" : "-");
    printf((s.st_mode & S_IXUSR) ? "x" : "-");
    printf((s.st_mode & S_IRGRP) ? "r" : "-");
    printf((s.st_mode & S_IWGRP) ? "w" : "-");
    printf((s.st_mode & S_IXGRP) ? "x" : "-");
    printf((s.st_mode & S_IROTH) ? "r" : "-");
    printf((s.st_mode & S_IWOTH) ? "w" : "-");
    printf((s.st_mode & S_IXOTH) ? "x" : "-");
    printf(")");
    printf("  Uid: %d   Gid: %d\n", s.st_uid, s.st_gid);

    printf("Access: %s", ctime((time_t *)&s.st_atime));
    printf("Modify: %s", ctime((time_t *)&s.st_mtime));
    printf("Change: %s", ctime((time_t *)&s.st_ctime));

    return 0;
}