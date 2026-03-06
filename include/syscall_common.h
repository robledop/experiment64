#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <drivers/terminal.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <task/process.h>

extern void (*syscall_exit_hook)(int);

#ifdef TEST_MODE
#define TEST_SYSCALL_LOG(fmt, ...)            \
    do                                        \
    {                                         \
        if (syscall_exit_hook)                \
            printk(fmt, ##__VA_ARGS__);       \
    } while (0)
#else
#define TEST_SYSCALL_LOG(fmt, ...) ((void)0)
#endif

bool user_ptr_write_ok(const void* dst, size_t size, const char* op);
bool user_ptr_read_ok(const void* src, size_t size, const char* op);
bool copy_to_user(void* dst, const void* src, size_t size);
bool copy_from_user(void* dst, const void* src, size_t size);
bool copy_from_user_str(char* dst, const char* src, size_t max_len);
bool map_user_anonymous_range(process_t* proc, pml4_t pml4, uint64_t start, uint64_t length, uint32_t vma_flags);
bool fd_can_read(const file_descriptor_t* desc);
bool fd_can_write(const file_descriptor_t* desc);
file_descriptor_t* fd_get(int fd);
void fd_put(file_descriptor_t* desc);
int fd_assign(file_descriptor_t* desc, int start_fd);
int split_parent_path(const char *path, char *parent, size_t parent_size);
void fill_stat_from_inode(const vfs_inode_t* inode, struct stat* st);
int resolve_user_path(const char* path, char* resolved, size_t size);
void set_process_name_from_path(process_t* proc, const char* path);
thread_t* find_thread_by_tid(process_t* proc, int tid);
bool thread_active_on_any_cpu(thread_t* t);
void free_thread_resources(thread_t* t);
bool futex_addr_ok(const uint32_t* uaddr, const char* op);
