#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "vfs.h"
#include "spinlock.h"

#define PIPE_BUF_SIZE 4096

typedef struct pipe
{
    spinlock_t lock;
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    int read_open;  // Number of readers
    int write_open; // Number of writers
} pipe_t;

// Create a new pipe and return read/write inodes
int pipe_alloc(vfs_inode_t **read_inode, vfs_inode_t **write_inode);

// Pipe inode operations
uint64_t pipe_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t pipe_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
void pipe_close(vfs_inode_t *node);
