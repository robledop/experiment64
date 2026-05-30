/**
 * @file common.c
 * @brief Syscall support core: user-pointer validation, user/kernel copies,
 *        and the eager anonymous page mapper.
 *
 * Despite the generic filename, this file holds subsystem-critical mmap logic.
 * Two distinct duties live here:
 *
 *   - Trust boundary: user_ptr_read_ok()/user_ptr_write_ok() (via the four-step
 *     user_ptr_access_ok() gauntlet) plus copy_to_user()/copy_from_user() and
 *     their _str/_checked variants — every syscall touching user memory routes
 *     through these.
 *   - Backing memory: map_user_anonymous_range() — the eager, per-page
 *     allocate/zero/map/record/unwind routine that actually backs anonymous
 *     mmap (kernel/syscalls/sys_mmap.c) as well as thread/exec stack setup.
 */
#include <drivers/terminal.h>
#include <lib/string.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <status.h>
#include <syscall_common.h>

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

    constexpr uint64_t mask       = PTE_ADDR_MASK;
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

/**
 * @brief Verify that a userland pointer range is safe to touch from kernel.
 *
 * The check is a four-step gauntlet; the range must survive every step:
 *   1. Basic sanity     — non-null, no integer overflow in addr+size.
 *   2. Canonical form   — both endpoints are x86_64 canonical addresses.
 *   3. Boundary check   — range lies entirely below the kernel (HHDM top) and
 *                         does not overlap this thread's kernel stack.
 *   4. Page-table walk  — every covered page has PTE_PRESENT|PTE_USER (plus
 *                         PTE_WRITABLE when @p write is true).
 *
 * Kernel threads (t->is_user == false) bypass the check; they trust their
 * own pointers.
 */
static bool user_ptr_access_ok(const void *ptr, size_t size, bool write, const char *op)
{
    // Step 1: basic sanity.
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

    // Step 2: canonical form (both endpoints, to catch size that crosses the gap).
    const uintptr_t last = (size == 0) ? addr : (end - 1);
    if (!addr_is_canonical(addr) || !addr_is_canonical(last)) {
        return false;
    }

    // Step 3: boundary check — range must not touch kernel space or kstack.
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

    // Step 4: page-table walk — every covered page must be user-accessible.
    if (!user_range_access_ok(current_process ? current_process->pml4 : nullptr, addr, size, write)) {
        return false;
    }
    return true;
}

static bool copy_to_user_impl(void *dst, const void *src, size_t size, const char *op)
{
    if (!dst || !src)
        return false;
    if (!user_ptr_write_ok(dst, size, op))
        return false;

    memcpy(dst, src, size);
    return true;
}

static bool copy_from_user_impl(void *dst, const void *src, size_t size, const char *op)
{
    if (!dst || !src)
        return false;
    if (!user_ptr_read_ok(src, size, op))
        return false;

    memcpy(dst, src, size);
    return true;
}

static bool copy_from_user_str_impl(char *dst, const char *src, size_t max_len, const char *op)
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
        if (!user_ptr_access_ok((const void *)addr, chunk, false, op))
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

bool user_ptr_write_ok(const void *dst, size_t size, const char *op)
{
    return user_ptr_access_ok(dst, size, true, op);
}

bool user_ptr_read_ok(const void *src, size_t size, const char *op)
{
    return user_ptr_access_ok(src, size, false, op);
}

int require_user_ptr_write(void *dst, size_t size, const char *op, int err)
{
    return user_ptr_write_ok(dst, size, op) ? ALL_OK : err;
}

int require_user_ptr_read(const void *src, size_t size, const char *op, int err)
{
    return user_ptr_read_ok(src, size, op) ? ALL_OK : err;
}

bool copy_to_user(void *dst, const void *src, size_t size)
{
    return copy_to_user_impl(dst, src, size, "copy_to_user");
}

bool copy_from_user(void *dst, const void *src, size_t size)
{
    return copy_from_user_impl(dst, src, size, "copy_from_user");
}

bool copy_from_user_str(char *dst, const char *src, size_t max_len)
{
    return copy_from_user_str_impl(dst, src, max_len, "copy_from_user_str");
}

int copy_to_user_checked(void *dst, const void *src, size_t size, const char *op, int err)
{
    return copy_to_user_impl(dst, src, size, op) ? ALL_OK : err;
}

int copy_from_user_checked(void *dst, const void *src, size_t size, const char *op, int err)
{
    return copy_from_user_impl(dst, src, size, op) ? ALL_OK : err;
}

int copy_from_user_str_checked(char *dst, const char *src, size_t max_len, const char *op, int err)
{
    return copy_from_user_str_impl(dst, src, max_len, op) ? ALL_OK : err;
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
