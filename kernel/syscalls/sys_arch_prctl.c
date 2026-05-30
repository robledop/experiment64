#include <syscall_common.h>
#include <arch/x86_64/cpu.h>
#include <sys/syscall.h>

/**
 * Set or get the FS base for the current thread.
 * @param code The operation to perform (ARCH_SET_FS or ARCH_GET_FS)
 * @param addr The address to set or get the FS base
 * @return 0 on success, -1 on failure 
 */
int sys_arch_prctl(int code, uint64_t addr)
{
    thread_t *t = current_thread;
    if (!t)
        return -1;

    switch (code) {
    case ARCH_SET_FS:
        if (!addr_is_canonical(addr))
            return -1;
        t->fs_base = addr;
        wrfsbase(addr);
        return 0;

    case ARCH_GET_FS:
        if (!user_ptr_write_ok((void *)addr, sizeof(uint64_t), "arch_prctl"))
            return -1;
        *(uint64_t *)addr = t->fs_base;
        return 0;

    default:
        return -1;
    }
}