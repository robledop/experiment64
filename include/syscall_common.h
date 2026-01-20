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
bool fd_can_read(const file_descriptor_t* desc);
bool fd_can_write(const file_descriptor_t* desc);
void fill_stat_from_inode(const vfs_inode_t* inode, struct stat* st);
int resolve_user_path(const char* path, char* resolved, size_t size);
void set_process_name_from_path(process_t* proc, const char* path);
