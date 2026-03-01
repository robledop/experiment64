#pragma once

#include <stdint.h>
#include <fs/vfs.h>
#include <task/spinlock.h>

#define PIPE_BUF_SIZE 4096

typedef struct pipe
{
    spinlock_t lock;
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    int read_open;      // Number of readers
    int write_open;     // Number of writers
    int nonblock_write; // Non-blocking writes (set via FIONBIO)
} pipe_t;

// Create a new pipe and return read/write inodes
int pipe_alloc(vfs_inode_t **read_inode, vfs_inode_t **write_inode);
