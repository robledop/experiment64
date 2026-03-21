#pragma once

#include <stddef.h>
#include <stdint.h>
#include <task/spinlock.h>
#include <fs/vfs.h>

#define SHM_NAME_MAX 64
#define SHM_MAX_OBJECTS 32

typedef struct shm_entry
{
    char name[SHM_NAME_MAX];
    uint64_t *phys_pages;
    size_t size;
    size_t num_pages;
    int refcount;
    bool marked_for_unlink;
} shm_entry_t;

void shm_init(void);
shm_entry_t *shm_lookup(const char *name);
shm_entry_t *shm_create(const char *name, size_t size);
shm_entry_t *shm_open_or_create(const char *name, int flags, size_t size);
void shm_ref(shm_entry_t *entry);
void shm_unref(shm_entry_t *entry);
int shm_do_unlink(const char *name);
bool shm_is_shm_inode(const vfs_inode_t *inode);

extern struct inode_operations shm_inode_ops;
