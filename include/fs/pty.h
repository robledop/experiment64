#pragma once

#include <fs/vfs.h>

int pty_alloc(vfs_inode_t **master_inode, vfs_inode_t **slave_inode);
