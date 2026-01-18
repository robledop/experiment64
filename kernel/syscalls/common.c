#include "syscall_common.h"
#include <lib/path.h>
#include <lib/string.h>
#include <mem/vmm.h>
#include <sys/fcntl.h>
#include <drivers/terminal.h>

/**
 * Check if a user pointer is writable within the current thread's context.
 * Returns true if the pointer is valid and writable, false otherwise.
 */
bool user_ptr_write_ok(const void* dst, size_t size, const char* op)
{
    if (!dst)
        return false;
    const thread_t* t = get_current_thread();
    const bool userish = (t != nullptr && t->is_user);
    if (!userish)
        return true;
    const uintptr_t addr = (uintptr_t)dst;
    const uintptr_t end = addr + size;
    if (end < addr)
        return false;

    const uintptr_t user_top = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
    const bool in_kernel = (addr >= user_top) || (end > user_top);

    const uintptr_t ktop = t->kstack_top;
    const uintptr_t kbase = (ktop != 0) ? (ktop - KSTACK_SIZE) : 0;
    const bool in_kstack = (ktop != 0) && (addr < ktop) && (end > kbase);

    if (in_kernel || in_kstack)
    {
        const process_t* p = get_current_process();
        printk("%s: bad dst=%p size=%zu pid=%d tid=%d in_kernel=%d in_kstack=%d ret=%p\n",
               op ? op : "user_ptr_write",
               dst,
               size,
               p != nullptr ? p->pid : -1,
               t->tid,
               in_kernel,
               in_kstack,
               __builtin_return_address(0));
        return false;
    }
    return true;
}

bool copy_to_user(void* dst, const void* src, size_t size)
{
    if (!dst || !src)
        return false;
    if (!user_ptr_write_ok(dst, size, "copy_to_user"))
        return false;
    memcpy(dst, src, size);
    return true;
}

bool fd_can_read(const file_descriptor_t* desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode != O_WRONLY;
}

bool fd_can_write(const file_descriptor_t* desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode == O_WRONLY || mode == O_RDWR || mode == (O_WRONLY | O_RDWR);
}

void fill_stat_from_inode(const vfs_inode_t* inode, struct stat* st)
{
    if (!inode || !st)
        return;

    // Try to use filesystem-specific stat if available
    if (inode->iops && inode->iops->stat)
    {
        if (inode->iops->stat(inode, st) == 0)
            return;
    }

    // Fallback to generic stat
    st->dev = 0;
    st->ino = (int)inode->inode;
    st->type = (int)(inode->flags & 0x07);
    st->nlink = 1;
    st->size = inode->size;
    st->ref = 0;
    st->i_atime = 0;
    st->i_ctime = 0;
    st->i_mtime = 0;
    st->i_dtime = 0;
    st->i_uid = 0;
    st->i_gid = 0;
    st->i_flags = 0;
}

void set_process_name_from_path(process_t* proc, const char* path)
{
    if (!proc || !path)
        return;
    const char* name = path;
    for (const char* p = path; *p; p++)
    {
        if (*p == '/' && p[1])
            name = p + 1;
    }
    path_safe_copy(proc->name, sizeof(proc->name), name);
}

// ReSharper disable once CppDFAConstantFunctionResult
// ReSharper disable once CppDFAConstantParameter
int resolve_user_path(const char* path, char* resolved, size_t size)
{
    if (!resolved || size == 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;

    const char* base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    path_build_absolute(base, path, resolved, size);
    return 0;
}
