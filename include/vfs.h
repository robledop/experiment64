#pragma once

#include <stdint.h>

#define VFS_MAX_PATH 256

#define VFS_FILE 0x01
#define VFS_DIRECTORY 0x02
#define VFS_CHARDEVICE 0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE 0x05
#define VFS_SYMLINK 0x06
#define VFS_MOUNTPOINT 0x08

struct stat
{
    int dev;
    int ino;
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

struct vfs_inode;

typedef struct
{
    char name[128];
    uint32_t inode;
} vfs_dirent_t;

struct inode_operations
{
    uint64_t (*read)(struct vfs_inode *node, uint64_t offset, uint64_t size, uint8_t *buffer);
    uint64_t (*write)(struct vfs_inode *node, uint64_t offset, uint64_t size, uint8_t *buffer);
    void (*open)(struct vfs_inode *node);
    void (*close)(struct vfs_inode *node);
    vfs_dirent_t *(*readdir)(struct vfs_inode *node, uint32_t index);
    struct vfs_inode *(*finddir)(struct vfs_inode *node, char *name);
    struct vfs_inode *(*clone)(struct vfs_inode *node);
    int (*mknod)(struct vfs_inode *node, char *name, int mode, int dev);
};

typedef struct vfs_inode
{
    uint32_t flags;
    uint32_t inode;
    uint64_t size;
    struct inode_operations *iops;
    struct vfs_inode *ptr; // Used for mountpoints and symlinks
    void *device;          // Private data for the driver
} vfs_inode_t;

extern vfs_inode_t *vfs_root;

void vfs_init();
uint64_t vfs_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
void vfs_open(vfs_inode_t *node);
void vfs_close(vfs_inode_t *node);
vfs_dirent_t *vfs_readdir(vfs_inode_t *node, uint32_t index);
vfs_inode_t *vfs_finddir(vfs_inode_t *node, char *name);
vfs_inode_t *vfs_resolve_path(const char *path);
int vfs_mknod(char *path, int mode, int dev);

void vfs_mount_root(void);
void vfs_register_mount(const char *name, vfs_inode_t *root);
