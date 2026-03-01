#pragma once

#include <stdint.h>

#define VFS_FILE 0x01
#define VFS_DIRECTORY 0x02
#define VFS_CHARDEVICE 0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE 0x05
#define VFS_SYMLINK 0x06
#define VFS_MOUNTPOINT 0x08

#ifndef S_IFMT
#define S_IFMT 00170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000
#define S_IRUSR 0000400
#define S_IWUSR 0000200
#define S_IXUSR 0000100
#define S_IRGRP 0000040
#define S_IWGRP 0000020
#define S_IXGRP 0000010
#define S_IROTH 0000004
#define S_IWOTH 0000002
#define S_IXOTH 0000001
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#endif

#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

static inline uint32_t vfs_mode_from_type(uint32_t type)
{
    switch (type & 0x07u) {
    case VFS_DIRECTORY:
        return S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    case VFS_CHARDEVICE:
        return S_IFCHR | S_IRUSR | S_IWUSR;
    case VFS_BLOCKDEVICE:
        return S_IFBLK | S_IRUSR | S_IWUSR;
    case VFS_PIPE:
        return S_IFIFO | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    case VFS_SYMLINK:
        return S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
    case VFS_FILE:
        return S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    default:
        return 0;
    }
}

struct stat
{
    int dev;
    int ino;
    uint32_t st_mode;
    int type;
    int nlink;
    uint64_t size;
    int ref;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    int i_uid;
    int i_gid;
    int i_flags;
};
typedef struct stat stat_t;

struct vfs_inode;

typedef struct
{
    char name[128];
    uint32_t inode;
} vfs_dirent_t;

struct inode_operations
{
    uint64_t (*read)(const struct vfs_inode *node, uint64_t offset, uint64_t size, uint8_t *buffer);
    uint64_t (*write)(struct vfs_inode *node, uint64_t offset, uint64_t size, uint8_t *buffer);
    int (*truncate)(struct vfs_inode *node);
    void (*open)(const struct vfs_inode *node);
    void (*close)(struct vfs_inode *node);
    int (*ioctl)(struct vfs_inode *node, int request, void *arg);
    int (*poll)(const struct vfs_inode *node, short events, short *revents);
    vfs_dirent_t *(*readdir)(const struct vfs_inode *node, uint32_t index);
    struct vfs_inode *(*finddir)(const struct vfs_inode *node, const char *name);
    struct vfs_inode *(*clone)(const struct vfs_inode *node);
    int (*mknod)(const struct vfs_inode *node, const char *name, int mode, int dev);
    int (*link)(struct vfs_inode *parent, const char *name, struct vfs_inode *target);
    int (*unlink)(struct vfs_inode *parent, const char *name);
    int (*stat)(const struct vfs_inode *node, struct stat *st);
    int (*rename)(struct vfs_inode *old_parent, const char *old_name,
                  struct vfs_inode *new_parent, const char *new_name);
};

typedef struct vfs_inode
{
    uint32_t flags;
    uint32_t inode;
    uint64_t size;
    uint32_t ref; // Reference count for dup() support
    struct inode_operations *iops;
    struct vfs_inode *ptr; // Used for mount points and symlinks
    void *device;          // Private data for the driver
} vfs_inode_t;

extern vfs_inode_t *vfs_root;

void vfs_init();
uint64_t vfs_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
int vfs_truncate(vfs_inode_t *node);
void vfs_open(const vfs_inode_t *node);
void vfs_close(vfs_inode_t *node);
int vfs_poll(const vfs_inode_t *node, short events, short *revents);
vfs_dirent_t *vfs_readdir(const vfs_inode_t *node, uint32_t index);
vfs_inode_t *vfs_finddir(vfs_inode_t *node, char *name);
vfs_inode_t *vfs_resolve_path(const char *path);
int vfs_mknod(char *path, int mode, int dev);
int vfs_ioctl(vfs_inode_t *node, int request, void *arg);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);

void vfs_mount_root(void);
void vfs_register_mount(const char *name, vfs_inode_t *root);
