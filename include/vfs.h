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

struct vfs_inode;

extern struct vfs_inode *vfs_root;

typedef struct
{
    char name[128];
    uint32_t inode;
} vfs_dirent_t;

typedef uint64_t (*vfs_read_t)(struct vfs_inode *node, uint64_t offset, uint64_t size, uint8_t *buffer);
typedef uint64_t (*vfs_write_t)(struct vfs_inode *node, uint64_t offset, uint64_t size, uint8_t *buffer);
typedef void (*vfs_open_t)(struct vfs_inode *node);
typedef void (*vfs_close_t)(struct vfs_inode *node);
typedef vfs_dirent_t *(*vfs_readdir_t)(struct vfs_inode *node, uint32_t index);
typedef struct vfs_inode *(*vfs_finddir_t)(struct vfs_inode *node, char *name);

typedef struct vfs_inode
{
    uint32_t flags;
    uint32_t inode;
    uint64_t size;
    vfs_read_t read;
    vfs_write_t write;
    vfs_open_t open;
    vfs_close_t close;
    vfs_readdir_t readdir;
    vfs_finddir_t finddir;
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

void vfs_mount_root(void);
