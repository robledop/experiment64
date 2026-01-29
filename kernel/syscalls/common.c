#include <syscall_common.h>
#include <lib/path.h>
#include <lib/string.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <mem/heap.h>
#include <sys/fcntl.h>
#include <drivers/terminal.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/cpu.h>

/**
 * Check if a user pointer is writable within the current thread's context.
 * Returns true if the pointer is valid and writable, false otherwise.
 */
bool user_ptr_write_ok(const void *dst, size_t size, const char *op)
{
    if (!dst)
        return false;
    const thread_t *t  = get_current_thread();
    const bool userish = (t != nullptr && t->is_user);
    if (!userish)
        return true;
    const uintptr_t addr = (uintptr_t)dst;
    const uintptr_t end  = addr + size;
    if (end < addr)
        return false;

    const uintptr_t user_top = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
    const bool in_kernel     = (addr >= user_top) || (end > user_top);

    const uintptr_t ktop  = t->kstack_top;
    const uintptr_t kbase = (ktop != 0) ? (ktop - KSTACK_SIZE) : 0;
    const bool in_kstack  = (ktop != 0) && (addr < ktop) && (end > kbase);

    if (in_kernel || in_kstack) {
        const process_t *p = get_current_process();
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

bool user_ptr_read_ok(const void *src, size_t size, const char *op)
{
    return user_ptr_write_ok(src, size, op ? op : "user_ptr_read");
}

bool copy_to_user(void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return false;
    if (!user_ptr_write_ok(dst, size, "copy_to_user"))
        return false;
    memcpy(dst, src, size);
    return true;
}

bool copy_from_user(void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return false;
    if (!user_ptr_read_ok(src, size, "copy_from_user"))
        return false;
    memcpy(dst, src, size);
    return true;
}

bool map_user_anonymous_range(process_t *proc, pml4_t pml4, uint64_t start, uint64_t length, uint32_t vma_flags)
{
    if (!proc || !pml4 || length == 0)
        return false;
    if ((start & (PAGE_SIZE - 1)) != 0 || (length & (PAGE_SIZE - 1)) != 0)
        return false;
    const uint64_t end = start + length;
    if (end < start)
        return false;

    uint64_t mapped_end = start;
    for (uint64_t virt = start; virt < end; virt += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys)
            goto fail;

        memset((void *)((uint64_t)phys + g_hhdm_offset), 0, PAGE_SIZE);
        vmm_map_page(pml4, virt, (uint64_t)phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
        if (vmm_virt_to_phys(pml4, virt) == 0) {
            pmm_free_page(phys);
            goto fail;
        }

        mapped_end = virt + PAGE_SIZE;
    }

    if (!vm_area_add(proc, start, end, vma_flags))
        goto fail;

    return true;

fail:
    for (uint64_t virt = start; virt < mapped_end; virt += PAGE_SIZE) {
        uint64_t phys = vmm_virt_to_phys(pml4, virt);
        if (phys)
            pmm_free_page((void *)(phys & ~(PAGE_SIZE - 1)));
        vmm_unmap_page(pml4, virt);
    }
    return false;
}

thread_t *find_thread_by_tid(process_t *proc, int tid)
{
    if (!proc || tid <= 0)
        return nullptr;

    thread_t *t;
    list_foreach_entry(t, &proc->threads, list) {
        if (t->tid == tid)
            return t;
    }

    return nullptr;
}

bool thread_active_on_any_cpu(thread_t *t)
{
    if (!t)
        return false;

    const uint32_t cpu_count = smp_get_cpu_count();
    for (uint32_t i = 0; i < cpu_count; i++) {
        cpu_t *cpu = smp_get_cpu_by_index(i);
        if (cpu && cpu->active_thread == t)
            return true;
    }

    return false;
}

void free_thread_resources(thread_t *t)
{
    if (!t)
        return;

    if (t->kstack_top != 0) {
        void *kstack_base = (void *)(t->kstack_top - KSTACK_SIZE);
        kfree(kstack_base);
    }
    kfree(t);
}

bool fd_can_read(const file_descriptor_t *desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode != O_WRONLY;
}

bool fd_can_write(const file_descriptor_t *desc)
{
    if (!desc)
        return false;
    const int mode = desc->flags & (O_WRONLY | O_RDWR);
    return mode == O_WRONLY || mode == O_RDWR || mode == (O_WRONLY | O_RDWR);
}

void fill_stat_from_inode(const vfs_inode_t *inode, struct stat *st)
{
    if (!inode || !st)
        return;

    // Try to use filesystem-specific stat if available
    if (inode->iops && inode->iops->stat) {
        if (inode->iops->stat(inode, st) == 0)
            return;
    }

    // Fallback to generic stat
    st->dev     = 0;
    st->ino     = (int)inode->inode;
    st->type    = (int)(inode->flags & 0x07);
    st->nlink   = 1;
    st->size    = inode->size;
    st->ref     = 0;
    st->i_atime = 0;
    st->i_ctime = 0;
    st->i_mtime = 0;
    st->i_dtime = 0;
    st->i_uid   = 0;
    st->i_gid   = 0;
    st->i_flags = 0;
}

void set_process_name_from_path(process_t *proc, const char *path)
{
    if (!proc || !path)
        return;
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' && p[1])
            name = p + 1;
    }
    path_safe_copy(proc->name, sizeof(proc->name), name);
}

// ReSharper disable once CppDFAConstantFunctionResult
// ReSharper disable once CppDFAConstantParameter
int resolve_user_path(const char *path, char *resolved, size_t size)
{
    if (!resolved || size == 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;

    const char *base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    path_build_absolute(base, path, resolved, size);
    return 0;
}

bool futex_addr_ok(const uint32_t *uaddr, const char *op)
{
    if (!uaddr)
        return false;
    if (((uintptr_t)uaddr & (sizeof(uint32_t) - 1)) != 0)
        return false;
    if (!user_ptr_read_ok(uaddr, sizeof(uint32_t), op))
        return false;
    return true;
}