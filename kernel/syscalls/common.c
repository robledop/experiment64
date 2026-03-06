#include <arch/x86_64/cpu.h>
#include <arch/x86_64/smp.h>
#include <drivers/terminal.h>
#include <lib/path.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <status.h>
#include <sys/fcntl.h>
#include <syscall_common.h>

static inline bool addr_is_canonical(uintptr_t addr)
{
    const uintptr_t upper = addr >> 47;
    return upper == 0 || upper == 0x1ffff;
}

static inline bool user_entry_ok(uint64_t entry, bool write)
{
    if ((entry & PTE_PRESENT) == 0)
        return false;
    if ((entry & PTE_USER) == 0)
        return false;
    if (write && (entry & PTE_WRITABLE) == 0)
        return false;
    return true;
}

static bool user_page_access_ok(const uint64_t *pml4, uintptr_t addr, bool write)
{
    if (!pml4)
        return false;

    const uint64_t mask       = 0x000FFFFFFFFFF000;
    const uint64_t *pml4_virt = (const uint64_t *)((uint64_t)pml4 + g_hhdm_offset);

    const size_t pml4_idx = (addr >> 39) & 0x1FF;
    const size_t pdpt_idx = (addr >> 30) & 0x1FF;
    const size_t pd_idx   = (addr >> 21) & 0x1FF;
    const size_t pt_idx   = (addr >> 12) & 0x1FF;

    const uint64_t pml4e = pml4_virt[pml4_idx];
    if (!user_entry_ok(pml4e, write))
        return false;

    const uint64_t *pdpt = (const uint64_t *)((pml4e & mask) + g_hhdm_offset);
    const uint64_t pdpte = pdpt[pdpt_idx];
    if (!user_entry_ok(pdpte, write))
        return false;
    if (pdpte & PTE_HUGE)
        return true;

    const uint64_t *pd = (const uint64_t *)((pdpte & mask) + g_hhdm_offset);
    const uint64_t pde = pd[pd_idx];
    if (!user_entry_ok(pde, write))
        return false;
    if (pde & PTE_HUGE)
        return true;

    const uint64_t *pt = (const uint64_t *)((pde & mask) + g_hhdm_offset);
    const uint64_t pte = pt[pt_idx];
    if (!user_entry_ok(pte, write))
        return false;

    return true;
}

static bool user_range_access_ok(const uint64_t *pml4, uintptr_t addr, size_t size, bool write)
{
    if (size == 0)
        return true;
    uintptr_t end  = addr + size - 1;
    uintptr_t page = addr & ~(PAGE_SIZE - 1);
    for (; page <= end; page += PAGE_SIZE) {
        if (!user_page_access_ok(pml4, page, write))
            return false;
    }
    return true;
}

static bool user_ptr_access_ok(const void *ptr, size_t size, bool write, const char *op)
{
    if (!ptr) {
        return false;
    }
    const thread_t *t  = get_current_thread();
    const bool userish = (t != nullptr && t->is_user);
    if (!userish) {
        return true;
    }
    const uintptr_t addr = (uintptr_t)ptr;
    const uintptr_t end  = addr + size;
    if (end < addr) {
        return false;
    }
    const uintptr_t last = (size == 0) ? addr : (end - 1);
    if (!addr_is_canonical(addr) || !addr_is_canonical(last)) {
        return false;
    }

    const uintptr_t user_top = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
    const bool in_kernel     = (addr >= user_top) || (end > user_top);

    const uintptr_t ktop  = t->kstack_top;
    const uintptr_t kbase = (ktop != 0) ? (ktop - KSTACK_SIZE) : 0;
    const bool in_kstack  = (ktop != 0) && (addr < ktop) && (end > kbase);

    if (in_kernel || in_kstack) {
        const process_t *p = get_current_process();
        const char *label  = op ? op : (write ? "user_ptr_write" : "user_ptr_read");
        printk("%s: bad dst=%p size=%zu pid=%d tid=%d in_kernel=%d in_kstack=%d ret=%p\n",
               label,
               ptr,
               size,
               p != nullptr ? p->pid : -1,
               t->tid,
               in_kernel,
               in_kstack,
               __builtin_return_address(0));
        return false;
    }
    if (!user_range_access_ok(current_process ? current_process->pml4 : nullptr, addr, size, write)) {
        return false;
    }
    return true;
}

/**
 * Check if a user pointer is writable within the current thread's context.
 * Returns true if the pointer is valid and writable, false otherwise.
 */
bool user_ptr_write_ok(const void *dst, size_t size, const char *op)
{
    return user_ptr_access_ok(dst, size, true, op);
}

bool user_ptr_read_ok(const void *src, size_t size, const char *op)
{
    return user_ptr_access_ok(src, size, false, op);
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

bool copy_from_user_str(char *dst, const char *src, size_t max_len)
{
    if (!dst || !src || max_len == 0)
        return false;

    size_t copied = 0;
    while (copied < max_len) {
        uintptr_t addr = (uintptr_t)src + copied;
        size_t offset  = addr & (PAGE_SIZE - 1);
        size_t chunk   = PAGE_SIZE - offset;
        if (chunk > max_len - copied)
            chunk = max_len - copied;
        if (!user_ptr_access_ok((const void *)addr, chunk, false, "copy_from_user_str"))
            return false;

        for (size_t i = 0; i < chunk; i++) {
            char c          = ((const char *)src)[copied + i];
            dst[copied + i] = c;
            if (c == '\0')
                return true;
        }
        copied += chunk;
    }

    dst[max_len - 1] = '\0';
    return false;
}

/**
 * Map a range of virtual addresses in the user space of a process to newly allocated physical pages, initializing them
 * to zero. The function ensures that the specified range is valid and properly aligned, and it updates the process
 * memory map with the new mapping. If any step fails, it cleans up any partially mapped pages and returns false.
 * @param proc The process whose address space is being modified.
 * @param pml4 The top-level page table of the process.
 * @param start The starting virtual address of the range to map.
 * @param length The length of the range to map.
 * @param vma_flags Flags for the virtual memory area.
 * @return true on success, false on failure (e.g. if the input parameters are invalid, if page allocation fails, or if
 * updating the process memory map fails).
 */
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
    list_foreach_entry(t, &proc->threads, list)
    {
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
        auto kstack_base = (void *)(t->kstack_top - KSTACK_SIZE);
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

/**
 * Get a file descriptor from the current process's fd table, incrementing its reference count if found.
 * @param fd The file descriptor number to retrieve.
 * @return The file descriptor, or nullptr if not found.
 */
file_descriptor_t *fd_get(int fd)
{
    if (!current_process)
        return nullptr;
    if (fd < 0 || fd >= MAX_FDS)
        return nullptr;

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(current_process->fd_lock, flags);
    file_descriptor_t *desc = current_process->fd_table[fd];
    if (desc)
        __atomic_add_fetch(&desc->ref, 1, __ATOMIC_RELAXED);
    SPIN_UNLOCK_INT_RESTORE(current_process->fd_lock, flags);
    return desc;
}

/**
 * Release a file descriptor, decrementing its reference count and freeing resources if it reaches zero.
 * @param desc The file descriptor to release.
 */
void fd_put(file_descriptor_t *desc)
{
    if (!desc)
        return;
    uint32_t ref = __atomic_sub_fetch(&desc->ref, 1, __ATOMIC_RELEASE);
    if (ref != 0)
        return;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    vfs_release(desc->inode);
    kfree(desc);
}

/**
 * Assign a file descriptor to the current process, starting from a specified fd number.
 * @param desc The file descriptor to assign.
 * @param start_fd The starting fd number to search from.
 * @return The assigned fd number, or -1 on failure.
 */
int fd_assign(file_descriptor_t *desc, int start_fd)
{
    if (!current_process || !desc)
        return -1;
    if (start_fd < 0)
        start_fd = 0;

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(current_process->fd_lock, flags);
    int fd = -1;
    for (int i = start_fd; i < MAX_FDS; i++) {
        if (current_process->fd_table[i] == nullptr) {
            current_process->fd_table[i] = desc;
            fd                           = i;
            break;
        }
    }
    SPIN_UNLOCK_INT_RESTORE(current_process->fd_lock, flags);
    return fd;
}

/**
 * Split a file path into its parent directory and the final component. The parent directory is copied into the provided
 * buffer.
 * @param path The file path to split.
 * @param parent The buffer to store the parent directory.
 * @param parent_size The size of the parent buffer.
 * @return 0 on success, or a negative error code on failure.
 */
int split_parent_path(const char *path, char *parent, size_t parent_size)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
        return -EBADPATH;
    if (last_slash == path) {
        if (last_slash[1] == '\0')
            return -EBADPATH;
        path_safe_copy(parent, parent_size, "/");
        return ALL_OK;
    }

    const size_t len = (size_t)(last_slash - path);
    if (len >= parent_size || last_slash[1] == '\0')
        return -EBADPATH;

    strncpy(parent, path, len);
    parent[len] = '\0';
    return ALL_OK;
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
    st->st_mode = vfs_mode_from_type((uint32_t)st->type);
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

/**
 * Set the process name based on the last component of a file path. The name is truncated if it exceeds the process name
 * buffer size.
 * @param proc The process to set the name for.
 * @param path The file path to extract the name from.
 */
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
/**
 * Resolve a user-provided file path to an absolute path, using the current process's working directory as the base if
 * necessary.
 * @param path The user-provided file path to resolve.
 * @param resolved The buffer to store the resolved absolute path.
 * @param size The size of the resolved buffer.
 * @return 0 on success, or -1 on failure (e.g. if the input path is invalid or cannot be resolved).
 */
int resolve_user_path(const char *path, char *resolved, size_t size)
{
    if (!resolved || size == 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;

    char input_buf[PATH_MAX];
    size_t max_len = size < sizeof(input_buf) ? size : sizeof(input_buf);
    if (!copy_from_user_str(input_buf, path, max_len))
        return -1;
    if (input_buf[0] == '\0')
        return -1;

    const char *base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    path_build_absolute(base, input_buf, resolved, size);
    return 0;
}

/**
 * Check if a user-provided address is valid for futex operations. The address must be non-null, 4-byte aligned, and
 * readable by the current thread.
 * @param uaddr The user-provided address to check.
 * @param op The name of the operation for logging purposes (e.g. "sys_futex"), or nullptr for a default label.
 * @return true if the address is valid for futex operations, false otherwise.
 */
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
