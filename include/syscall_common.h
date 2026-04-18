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

/** Check whether a user destination range is writable for the current syscall context. */
bool user_ptr_write_ok(const void* dst, size_t size, const char* op);
/** Check whether a user source range is readable for the current syscall context. */
bool user_ptr_read_ok(const void* src, size_t size, const char* op);
/** Return `ALL_OK` when a user destination range is writable, otherwise return `err`. */
int require_user_ptr_write(void *dst, size_t size, const char *op, int err);
/** Return `ALL_OK` when a user source range is readable, otherwise return `err`. */
int require_user_ptr_read(const void *src, size_t size, const char *op, int err);
/** Copy a fixed-size buffer into user memory after validating the destination range. */
bool copy_to_user(void* dst, const void* src, size_t size);
/** Copy a fixed-size buffer from user memory after validating the source range. */
bool copy_from_user(void* dst, const void* src, size_t size);
/** Copy a NUL-terminated string from user memory into a kernel buffer. */
bool copy_from_user_str(char* dst, const char* src, size_t max_len);
/** Checked `copy_to_user` variant that returns `err` instead of `false` on failure. */
int copy_to_user_checked(void *dst, const void *src, size_t size, const char *op, int err);
/** Checked `copy_from_user` variant that returns `err` instead of `false` on failure. */
int copy_from_user_checked(void *dst, const void *src, size_t size, const char *op, int err);
/** Checked `copy_from_user_str` variant that returns `err` instead of `false` on failure. */
int copy_from_user_str_checked(char *dst, const char *src, size_t max_len, const char *op, int err);
/** Map a zeroed anonymous user range and register the matching VMA. */
bool map_user_anonymous_range(process_t* proc, pml4_t pml4, uint64_t start, uint64_t length, uint32_t vma_flags);
/** Return whether a descriptor mode allows read access. */
bool fd_can_read(const file_descriptor_t* desc);
/** Return whether a descriptor mode allows write access. */
bool fd_can_write(const file_descriptor_t* desc);
/** Allocate and initialize a new file descriptor wrapper. */
file_descriptor_t *fd_alloc(vfs_inode_t *inode, int flags);
/** Acquire a process file descriptor and bump its reference count. */
file_descriptor_t* fd_get(int fd);
/** Drop a file descriptor reference and free it when the count reaches zero. */
void fd_put(file_descriptor_t* desc);
/** Install a descriptor into the current process fd table starting at `start_fd`. */
int fd_assign(file_descriptor_t* desc, int start_fd);
/** Extract the parent directory path for an absolute path. */
int split_parent_path(const char *path, char *parent, size_t parent_size);
/** Fill a userspace-facing `stat` structure from an inode, using the fs hook when present. */
void fill_stat_from_inode(const vfs_inode_t* inode, struct stat* st);
/** Resolve a user path against the current working directory into an absolute kernel buffer. */
int resolve_user_path(const char* path, char* resolved, size_t size);
/** Validate a user path pointer and resolve it into an absolute kernel buffer. */
int resolve_user_path_checked(const char *path, char *resolved, size_t size, const char *op);
/** Update a process name from the final component of a path. */
void set_process_name_from_path(process_t* proc, const char* path);
/** Find a thread in a process thread list by TID. */
thread_t* find_thread_by_tid(process_t* proc, int tid);
/** Return whether a thread is currently active on any CPU. */
bool thread_active_on_any_cpu(thread_t* t);
/** Release the heap-owned resources associated with a terminated thread. */
void free_thread_resources(thread_t* t);
/** Check whether a futex address points at a readable user `uint32_t`. */
bool futex_addr_ok(const uint32_t* uaddr, const char* op);

/**
 * @brief Shared core of the wait(), waitpid(), and wait4() syscalls.
 *
 * Blocks or polls for a terminated child process, optionally a specific one,
 * copies its exit code and (if @p info is non-null) its crash_info into user
 * memory, and reaps it. Handles SIGCHLD=SIG_IGN / SA_NOCLDWAIT, WNOHANG,
 * and the "zombie currently un-reapable" race by yielding.
 *
 * @param pid     -1 = any child; >0 = the specific child; other = -1.
 * @param status  User pointer to receive the exit code, or null to skip.
 * @param options Bitmask. Only WNOHANG is supported; others cause -1.
 * @param info    User pointer to receive crash_info, or null to skip. Only
 *                wait4() passes a non-null value here.
 * @param op      Short label for TEST_SYSCALL_LOG messages ("sys_wait", ...).
 * @return pid of the reaped child, 0 (WNOHANG and child still running),
 *         or -1 on any form of failure.
 */
int wait_for_child(int pid, int *status, int options, crash_info_t *info, const char *op);
