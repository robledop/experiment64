#include <sys/syscall.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <debug.h>
#include <fs/vfs.h>
#include <net/socket.h>
#include <sys/time.h>
#include <task/process.h>
#include "syscall_common.h"

extern void syscall_entry(void);

#ifdef TEST_MODE
extern volatile const char* g_current_test_name;
volatile uint64_t test_syscall_count = 0;
volatile uint64_t test_syscall_last_num = 0;
volatile uint64_t test_syscall_last_arg1 = 0;
#endif

uint8_t bootstrap_stack[4096];

void (*syscall_exit_hook)(int) = nullptr;


void syscall_set_stack(uint64_t stack)
{
    cpu_t* cpu = get_cpu();
    cpu->kernel_rsp = stack;
    tss_set_stack(stack);
}

void syscall_init(void)
{
    // Enable SCE (System Call Extensions) - Bit 0 of EFER
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1;
    wrmsr(MSR_EFER, efer);

    // Set STAR MSR
    // Bits 63:48 - User Code Segment Base (0x10) -> CS=0x20, SS=0x18
    // Bits 47:32 - Kernel Code Segment Base (0x08) -> CS=0x08, SS=0x10
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);

    // Set LSTAR MSR - Target RIP for syscall
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    // Set SFMASK MSR - RFLAGS mask
    // Mask Interrupts (IF - bit 9)
    wrmsr(MSR_SFMASK, RFLAGS_IF);

    // Set TSS RSP0 to the kernel stack
    // For BSP, we use bootstrap stack initially
    // For APs, we should probably have a stack allocated or use what's set
    cpu_t* cpu = get_cpu();
    if (cpu->kernel_rsp == 0)
    {
        // For BSP, use bootstrap_stack if no stack is set.
        if (cpu->lapic_id == 0)
        {
            cpu->kernel_rsp = (uint64_t)bootstrap_stack + sizeof(bootstrap_stack);
            tss_set_stack(cpu->kernel_rsp);
        }
    }
}

void syscall_set_exit_hook(void (*hook)(int))
{
    syscall_exit_hook = hook;
}

uint64_t syscall_handler(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                         struct syscall_regs* regs)
{
    // Enable interrupts to allow I/O
    __asm__ volatile("sti");

#ifdef TEST_MODE
    test_syscall_count++;
    test_syscall_last_num = syscall_number;
    test_syscall_last_arg1 = arg1;
#endif

    uint64_t arg4 = regs ? regs->r10 : 0;
    uint64_t arg5 = regs ? regs->r8 : 0;
    uint64_t arg6 = regs ? regs->r9 : 0;

    switch (syscall_number)
    {
    case SYS_WRITE:
        return sys_write((int)arg1, (const char*)arg2, (size_t)arg3);
    case SYS_EXIT:
        sys_exit((int)arg1);
        return 0;
    case SYS_EXEC:
        sys_exec((const char*)arg1, regs);
        return 0;
    case SYS_EXECVE:
        return sys_execve((const char*)arg1, (const char*const *)arg2, (const char*const *)arg3, regs);
    case SYS_FORK:
        return sys_fork(regs);
    case SYS_SPAWN:
        return sys_spawn((const char*)arg1);
    case SYS_WAIT:
        return sys_wait((int*)arg1);
    case SYS_GETPID:
        return sys_getpid();
    case SYS_YIELD:
        yield();
        return 0;
    case SYS_READ:
        return sys_read((int)arg1, (char*)arg2, (size_t)arg3);
    case SYS_SBRK:
        return sys_sbrk((int64_t)arg1);
    case SYS_OPEN:
        return sys_open((const char*)arg1, (int)arg2);
    case SYS_CLOSE:
        return sys_close((int)arg1);
    case SYS_READDIR:
        return sys_readdir((int)arg1, (vfs_dirent_t*)arg2);
    case SYS_CHDIR:
        return sys_chdir((const char*)arg1);
    case SYS_SLEEP:
        return sys_sleep(arg1);
    case SYS_USLEEP:
        return sys_usleep(arg1);
    case SYS_MKNOD:
        return sys_mknod((const char*)arg1, (int)arg2, (int)arg3);
    case SYS_IOCTL:
        return sys_ioctl((int)arg1, (int)arg2, (void*)arg3);
    case SYS_MMAP:
        return (uint64_t)sys_mmap((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (size_t)arg6);
    case SYS_MUNMAP:
        return (uint64_t)sys_munmap((void*)arg1, (size_t)arg2);
    case SYS_STAT:
        return sys_stat((const char*)arg1, (struct stat*)arg2);
    case SYS_FSTAT:
        return sys_fstat((int)arg1, (struct stat*)arg2);
    case SYS_LINK:
        return sys_link((const char*)arg1, (const char*)arg2);
    case SYS_UNLINK:
        return sys_unlink((const char*)arg1);
    case SYS_GETCWD:
        return sys_getcwd((char*)arg1, (size_t)arg2);
    case SYS_GETTIMEOFDAY:
        return sys_gettimeofday((struct timeval*)arg1, (struct timezone*)arg2);
    case SYS_PIPE:
        return sys_pipe((int*)arg1);
    case SYS_LSEEK:
        return sys_lseek((int)arg1, (long)arg2, (int)arg3);
    case SYS_DUP:
        return sys_dup((int)arg1);
    case SYS_SHUTDOWN:
        sys_shutdown();
        return 0;
    case SYS_REBOOT:
        sys_reboot();
        return 0;
    case SYS_KILL:
        return sys_kill((int)arg1, (int)arg2);
    case SYS_SOCKET:
        return sys_socket((int)arg1, (int)arg2, (int)arg3);
    case SYS_BIND:
        return sys_bind((int)arg1, (const struct sockaddr*)arg2, (size_t)arg3);
    case SYS_LISTEN:
        return sys_listen((int)arg1, (int)arg2);
    case SYS_ACCEPT:
        return sys_accept((int)arg1, (struct sockaddr*)arg2, (size_t)arg3);
    case SYS_SENDTO:
        return sys_sendto((int)arg1, (const void*)arg2, (size_t)arg3, (int)arg4,
                          (const struct sockaddr*)arg5, (socklen_t)arg6);
    case SYS_RECVFROM:
        return sys_recvfrom((int)arg1, (void*)arg2, (size_t)arg3, (int)arg4,
                            (struct sockaddr*)arg5, (socklen_t*)arg6);
    default:
        panic("Unknown syscall: %lu\n", syscall_number);
        // ReSharper disable once CppDFAUnreachableCode
        __builtin_unreachable();
    }
}
