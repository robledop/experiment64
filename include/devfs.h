#pragma once

#include "vfs.h"

void devfs_init(void);
void devfs_register_device(const char *name, vfs_inode_t *device_node);
