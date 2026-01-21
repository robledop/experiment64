#include <sys/syscall.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <debug.h>
#include <fs/vfs.h>
#include <net/socket.h>
#include <sys/time.h>
#include <task/process.h>
#include <task/signal.h>
#include <syscall_common.h>

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

    uint64_t ret = 0;
    switch (syscall_number)
    {
    case SYS_WRITE:
        ret = sys_write((int)arg1, (const char*)arg2, (size_t)arg3);
        break;
    case SYS_EXIT:
        sys_exit((int)arg1);
        return 0;
    case SYS_EXEC:
        ret = sys_exec((const char*)arg1, regs);
        break;
    case SYS_EXECVE:
        ret = sys_execve((const char*)arg1, (const char*const *)arg2, (const char*const *)arg3, regs);
        break;
    case SYS_FORK:
        ret = sys_fork(regs);
        break;
    case SYS_SPAWN:
        ret = sys_spawn((const char*)arg1);
        break;
    case SYS_WAIT:
        ret = sys_wait((int*)arg1);
        break;
    case SYS_GETPID:
        ret = sys_getpid();
        break;
    case SYS_YIELD:
        yield();
        ret = 0;
        break;
    case SYS_READ:
        ret = sys_read((int)arg1, (char*)arg2, (size_t)arg3);
        break;
    case SYS_SBRK:
        ret = sys_sbrk((int64_t)arg1);
        break;
    case SYS_OPEN:
        ret = sys_open((const char*)arg1, (int)arg2);
        break;
    case SYS_CLOSE:
        ret = sys_close((int)arg1);
        break;
    case SYS_READDIR:
        ret = sys_readdir((int)arg1, (vfs_dirent_t*)arg2);
        break;
    case SYS_CHDIR:
        ret = sys_chdir((const char*)arg1);
        break;
    case SYS_SLEEP:
        ret = sys_sleep(arg1);
        break;
    case SYS_USLEEP:
        ret = sys_usleep(arg1);
        break;
    case SYS_MKNOD:
        ret = sys_mknod((const char*)arg1, (int)arg2, (int)arg3);
        break;
    case SYS_IOCTL:
        ret = sys_ioctl((int)arg1, (int)arg2, (void*)arg3);
        break;
    case SYS_MMAP:
        ret = (uint64_t)sys_mmap((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (size_t)arg6);
        break;
    case SYS_MUNMAP:
        ret = (uint64_t)sys_munmap((void*)arg1, (size_t)arg2);
        break;
    case SYS_STAT:
        ret = sys_stat((const char*)arg1, (struct stat*)arg2);
        break;
    case SYS_FSTAT:
        ret = sys_fstat((int)arg1, (struct stat*)arg2);
        break;
    case SYS_LINK:
        ret = sys_link((const char*)arg1, (const char*)arg2);
        break;
    case SYS_UNLINK:
        ret = sys_unlink((const char*)arg1);
        break;
    case SYS_GETCWD:
        ret = sys_getcwd((char*)arg1, (size_t)arg2);
        break;
    case SYS_GETTIMEOFDAY:
        ret = sys_gettimeofday((struct timeval*)arg1, (struct timezone*)arg2);
        break;
    case SYS_PIPE:
        ret = sys_pipe((int*)arg1);
        break;
    case SYS_LSEEK:
        ret = sys_lseek((int)arg1, (long)arg2, (int)arg3);
        break;
    case SYS_DUP:
        ret = sys_dup((int)arg1);
        break;
    case SYS_SHUTDOWN:
        sys_shutdown();
        ret = 0;
        break;
    case SYS_REBOOT:
        sys_reboot();
        ret = 0;
        break;
    case SYS_KILL:
        ret = sys_kill((int)arg1, (int)arg2);
        break;
    case SYS_SOCKET:
        ret = sys_socket((int)arg1, (int)arg2, (int)arg3);
        break;
    case SYS_BIND:
        ret = sys_bind((int)arg1, (const struct sockaddr*)arg2, (size_t)arg3);
        break;
    case SYS_LISTEN:
        ret = sys_listen((int)arg1, (int)arg2);
        break;
    case SYS_ACCEPT:
        ret = sys_accept((int)arg1, (struct sockaddr*)arg2, (size_t)arg3);
        break;
    case SYS_SENDTO:
        ret = sys_sendto((int)arg1, (const void*)arg2, (size_t)arg3, (int)arg4,
                         (const struct sockaddr*)arg5, (socklen_t)arg6);
        break;
    case SYS_RECVFROM:
        ret = sys_recvfrom((int)arg1, (void*)arg2, (size_t)arg3, (int)arg4,
                           (struct sockaddr*)arg5, (socklen_t*)arg6);
        break;
    case SYS_SIGACTION:
        ret = sys_sigaction((int)arg1, (const sigaction_t*)arg2, (sigaction_t*)arg3);
        break;
    case SYS_SIGRETURN:
        ret = sys_sigreturn((const sigcontext_t*)arg1, regs);
        break;
    case SYS_THREAD_CREATE:
        ret = sys_thread_create(arg1, arg2);
        break;
    case SYS_THREAD_EXIT:
        sys_thread_exit((int)arg1);
        return 0;
    case SYS_THREAD_JOIN:
        ret = sys_thread_join((int)arg1, (int*)arg2);
        break;
    default:
        panic("Unknown syscall: %lu\n", syscall_number);
        // ReSharper disable once CppDFAUnreachableCode
        __builtin_unreachable();
    }

    signal_deliver_syscall(regs, &ret);
    return ret;
}
