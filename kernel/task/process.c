#include <task/process.h>
#include <task/signal.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <mem/vmm.h>
#include <syscall_common.h>
#include <debug.h>

list_item_t process_list __attribute__((aligned(16))) = LIST_HEAD_INIT(process_list);
process_t *kernel_process                             = nullptr;
process_t *init_process                               = nullptr;
int next_pid                                           = 1;

/**
 * Initialize the virtual memory area list for a process.
 * @param proc
 */
void vm_area_init(process_t *proc)
{
    if (!proc)
        return;
    spinlock_init(&proc->vm_lock);
    list_init_head(&proc->vm_areas);
    proc->vm_area_count = 0;
}

/**
 * Add a virtual memory area to a process.
 * @param proc Process to add the area to
 * @param start Start address
 * @param end End address
 * @param flags Protection flags
 * @return
 */
vm_area_t *vm_area_add(process_t *proc, uint64_t start, uint64_t end, uint32_t flags)
{
    if (!proc || start >= end)
        return nullptr;

    spinlock_acquire(&proc->vm_lock);
    vm_area_t *existing;
    list_foreach_entry(existing, &proc->vm_areas, list) {
        if (!(end <= existing->start || start >= existing->end)) {
            spinlock_release(&proc->vm_lock);
            return nullptr; // overlap
        }
    }

    vm_area_t *area = kmalloc(sizeof(vm_area_t));
    if (!area) {
        spinlock_release(&proc->vm_lock);
        return nullptr;
    }

    area->start = start;
    area->end   = end;
    area->flags = flags;
    list_add_tail(&area->list, &proc->vm_areas);
    proc->vm_area_count++;
    spinlock_release(&proc->vm_lock);
    return area;
}

/**
 * Clone the virtual memory areas from one process to another.
 * @param dest Destination process
 * @param src Source process
 */
void vm_area_clone(process_t *dest, process_t *src)
{
    if (!dest || !src)
        return;

    spinlock_acquire(&dest->vm_lock);
    list_init_head(&dest->vm_areas);
    dest->vm_area_count = 0;
    spinlock_release(&dest->vm_lock);

    spinlock_acquire(&src->vm_lock);
    vm_area_t *area;
    list_foreach_entry(area, &src->vm_areas, list) {
        vm_area_add(dest, area->start, area->end, area->flags);
    }
    spinlock_release(&src->vm_lock);
}

/**
 * Clear all virtual memory areas for a process.
 * @param proc Process to clear areas for
 */
void vm_area_clear(process_t *proc)
{
    if (!proc)
        return;

    spinlock_acquire(&proc->vm_lock);
    vm_area_t *area, *tmp;
    list_foreach_entry_safe(area, tmp, &proc->vm_areas, list) {
        list_del(&area->list);
        kfree(area);
    }
    list_init_head(&proc->vm_areas);
    proc->vm_area_count = 0;
    spinlock_release(&proc->vm_lock);
}

process_t *process_create(const char *name)
{
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc)
        return nullptr;
    memset(proc, 0, sizeof(process_t));
    spinlock_init(&proc->fd_lock);
    vm_area_init(proc);
    signal_init_process(proc);

    spinlock_acquire(&scheduler_lock);
    proc->pid = next_pid++;
    spinlock_release(&scheduler_lock);

    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';

    process_t *current = get_current_process();
    if (current && current->cwd[0]) {
        strncpy(proc->cwd, current->cwd, PATH_MAX - 1);
        proc->cwd[PATH_MAX - 1] = '\0';
    } else {
        proc->cwd[0] = '/';
        proc->cwd[1] = '\0';
    }

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    list_init_head(&proc->threads);
    list_add_tail(&proc->list, &process_list);
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    return proc;
}

void process_copy_fds(process_t *dest, process_t *src)
{
    file_descriptor_t *fds[MAX_FDS] = {nullptr};
    uint64_t fd_flags;
    SPIN_LOCK_INT_SAVE(src->fd_lock, fd_flags);
    for (int i = 0; i < MAX_FDS; i++) {
        fds[i] = src->fd_table[i];
        if (fds[i])
            __atomic_add_fetch(&fds[i]->ref, 1, __ATOMIC_RELAXED);
    }
    SPIN_UNLOCK_INT_RESTORE(src->fd_lock, fd_flags);

    for (int i = 0; i < MAX_FDS; i++) {
        if (fds[i]) {
            file_descriptor_t *old_desc = fds[i];
            file_descriptor_t *new_desc = kmalloc(sizeof(file_descriptor_t));
            if (new_desc) {
                memset(new_desc, 0, sizeof(file_descriptor_t));
                new_desc->flags  = old_desc->flags;
                new_desc->offset = old_desc->offset;
                new_desc->ref    = 1;

                if (old_desc->inode) {
                    // For pipes and special files, share the inode
                    // For regular files with a clone op, clone it
                    // Otherwise copy the inode
                    if (old_desc->inode->flags & VFS_PIPE) {
                        // Pipes are shared across fork - just increment ref
                        new_desc->inode = old_desc->inode;
                        old_desc->inode->ref++;
                    } else if (old_desc->inode->iops && old_desc->inode->iops->clone) {
                        new_desc->inode = old_desc->inode->iops->clone(old_desc->inode);
                        if (new_desc->inode)
                            new_desc->inode->ref = 1;
                    } else {
                        new_desc->inode = kmalloc(sizeof(vfs_inode_t));
                        if (new_desc->inode) {
                            memcpy(new_desc->inode, old_desc->inode, sizeof(vfs_inode_t));
                            new_desc->inode->ref = 1;
                        }
                    }
                } else {
                    kfree(new_desc);
                    dest->fd_table[i] = nullptr;
                    continue;
                }
                if (!new_desc->inode) {
                    kfree(new_desc);
                    dest->fd_table[i] = nullptr;
                    continue;
                }
                dest->fd_table[i] = new_desc;
            } else {
                dest->fd_table[i] = nullptr;
            }
            fd_put(old_desc);
        } else {
            dest->fd_table[i] = nullptr;
        }
    }
}

static void process_collect_threads_locked(const process_t *proc, list_item_t *free_list)
{
    spinlock_assert_held(&scheduler_lock);
    thread_t *t, *next_t;
    list_foreach_entry_safe(t, next_t, &proc->threads, list) {
        if (!t)
            panic("%s: thread is null", __func__);

        list_del(&t->list);
        list_add_tail(&t->list, free_list);

        thread_state_store(t, THREAD_TERMINATED);
        t->process        = nullptr;
        t->saved_user_rsp = 0;
    }
}

static void process_destroy_now(process_t *proc)
{
    list_item_t free_list = LIST_HEAD_INIT(free_list);
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

    // Re-verify that no thread is active on any CPU before destroying.
    // The state could have changed between process_can_reap_locked() and now.
    if (!process_can_reap_locked(proc)) {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
        return;
    }

    if (process_in_list(proc))
        list_del(&proc->list);

    process_collect_threads_locked(proc, &free_list);

    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

    thread_t *t, *next_t;
    list_foreach_entry_safe(t, next_t, &free_list, list) {
        if (!t)
            panic("%s: thread is null", __func__);

        list_del(&t->list);
        auto kernel_stack_base = (void *)(t->kstack_top - KSTACK_SIZE);
        kfree(kernel_stack_base);
        kfree(t);
    }

    file_descriptor_t *fds[MAX_FDS] = {nullptr};
    uint64_t fd_flags;
    SPIN_LOCK_INT_SAVE(proc->fd_lock, fd_flags);
    for (int i = 0; i < MAX_FDS; i++) {
        fds[i] = proc->fd_table[i];
        proc->fd_table[i] = nullptr;
    }
    SPIN_UNLOCK_INT_RESTORE(proc->fd_lock, fd_flags);

    for (int i = 0; i < MAX_FDS; i++) {
        if (fds[i])
            fd_put(fds[i]);
    }

    vm_area_clear(proc);

    if (proc->pml4 && proc->pid != 1) {
        vmm_destroy_pml4(proc->pml4);
    }

    kfree(proc);
}

void process_destroy(process_t *proc)
{
    if (!proc)
        return;

    if (proc == kernel_process || (init_process && proc == init_process))
        return;

    for (;;) {
        uint64_t rflags;
        SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

        thread_t *t;
        list_foreach_entry(t, &proc->threads, list) {
            thread_state_store(t, THREAD_TERMINATED);
        }

        const bool can_reap = process_can_reap_locked(proc);
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);

        if (can_reap)
            break;

        yield();
    }

    process_destroy_now(proc);
}

void process_reap(process_t *proc)
{
    if (!proc) {
        return;
    }
    process_destroy_now(proc);
}

void process_mark_exited_locked(process_t *proc, int exit_code, process_t **parent_out)
{
    spinlock_assert_held(&scheduler_lock);
    if (!proc)
        return;

    proc->exit_code  = exit_code;
    proc->terminated = true;
    signal_send_sigchld(proc);

    thread_t *t;
    list_foreach_entry(t, &proc->threads, list) {
        thread_state_store(t, THREAD_TERMINATED);
    }

    process_t *new_parent = init_process ? init_process : kernel_process;
    process_t *child;
    list_foreach_entry(child, &process_list, list) {
        if (child && child->parent == proc) {
            child->parent = new_parent;
            if (child->terminated)
                thread_wakeup_locked(new_parent);
        }
    }

    if (parent_out)
        *parent_out = proc->parent;
}

static const char *thread_state_str(thread_state_t state)
{
    switch (state) {
    case THREAD_READY:
        return "READY";
    case THREAD_RUNNING:
        return "RUN";
    case THREAD_BLOCKED:
        return "SLEEP";
    case THREAD_TERMINATED:
        return "DEAD";
    default:
        return "?";
    }
}

void process_dump(void)
{
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);
    printk("\n%-5s %-5s %-5s %-6s %s\n", "PID", "TID", "CPU", "STATE", "NAME");
    process_t *p;
    list_foreach_entry(p, &process_list, list) {
        thread_t *t;
        list_foreach_entry(t, &p->threads, list) {
            uint32_t raw_state    = thread_state_load_raw(t);
            const char *state_str = "BAD";

            if (thread_state_valid_raw(raw_state)) {
                state_str = thread_state_str((thread_state_t)raw_state);
            }

            printk("%-5d %-5d %-5d %-6s %s\n",
                   p->pid,
                   t->tid,
                   t->last_cpu,
                   state_str,
                   p->name);
        }
    }
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
}
