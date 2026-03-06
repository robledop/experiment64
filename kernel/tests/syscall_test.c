#include <tests/test.h>
#include <sys/syscall.h>
#include <lib/string.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <drivers/terminal.h>
#include <task/process.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <fs/vfs.h>
#include <sys/mman.h>
#include <tests/test_util.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <net/network.h>
#include <net/ethernet.h>
#include <net/ipv4.h>
#include <arpa/inet.h>
#include <status.h>
#ifdef TEST_MODE
#include <drivers/tsc.h>
#endif

// Buffer for setjmp/longjmp
static void *test_env[64];
static volatile int test_exit_code    = 0;
static int test_runner_pid            = 0;
static uint64_t syscall_test_rflags   = 0;
static bool syscall_test_rflags_valid = false;

static inline void syscall_test_disable_interrupts(void)
{
    uint64_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags)::"memory");
    __asm__ volatile("cli" ::: "memory");
    syscall_test_rflags       = flags;
    syscall_test_rflags_valid = true;
}

static inline void syscall_test_restore_interrupts(void)
{
    if (!syscall_test_rflags_valid)
        return;
    if (syscall_test_rflags & (1 << 9))
        __asm__ volatile("sti" ::: "memory");
    syscall_test_rflags_valid = false;
}

static void enter_user_mode(uint64_t rip, uint64_t rsp)
{
    constexpr uint64_t user_cs = 0x20 | 3;
    constexpr uint64_t user_ss = 0x18 | 3;
    constexpr uint64_t rflags  = 0x202;


    __asm__ volatile(
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %0\n"
        "push %1\n"
        "push %2\n"
        "push %3\n"
        "push %4\n"
        "iretq\n"
        :
        : "r"(user_ss), "r"(rsp), "r"(rflags), "r"(user_cs), "r"(rip)
        : "memory", "rax", "rdx");
    __builtin_unreachable();
}

static void syscall_test_prepare_longjmp(void)
{
    syscall_test_disable_interrupts();
}

static void syscall_test_resume_after_longjmp(void)
{
    syscall_test_restore_interrupts();
}

static uint8_t write_exit_stub_bytes[] = {
    0xB8, 0x00, 0x00, 0x00, 0x00,                               // mov eax, 0 (SYS_WRITE)
    0xBF, 0x01, 0x00, 0x00, 0x00,                               // mov edi, 1
    0x48, 0xBE, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rsi, 0x400100
    0xBA, 0x0C, 0x00, 0x00, 0x00,                               // mov edx, 12
    0x0F, 0x05,                                                 // syscall

    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x2A, 0x00, 0x00, 0x00, // mov edi, 42
    0x0F, 0x05                    // syscall
};

static uint8_t getpid_stub_bytes[] = {
    0xB8, 0x06, 0x00, 0x00, 0x00, // mov eax, 6 (SYS_GETPID)
    0x0F, 0x05,                   // syscall
    // RAX now has PID. Move to EDI for exit code.
    0x89, 0xC7,                   // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static uint8_t yield_stub_bytes[] = {
    0xB8, 0x07, 0x00, 0x00, 0x00, // mov eax, 7 (SYS_YIELD)
    0x0F, 0x05,                   // syscall
    // If we return, it worked. Exit with 0.
    0xBF, 0x00, 0x00, 0x00, 0x00, // mov edi, 0
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static uint8_t spawn_stub_bytes[] = {
    0xB8, 0x08, 0x00, 0x00, 0x00, // mov eax, 8 (SYS_SPAWN)
    0xBF, 0x00, 0x01, 0x40, 0x00, // mov edi, 0x400100 (path)
    0x0F, 0x05,                   // syscall

    0x50,                         // push rax (Save PID)
    0x48, 0x83, 0xEC, 0x08,       // sub rsp, 8
    0x48, 0x89, 0xE7,             // mov rdi, rsp
    0xB8, 0x05, 0x00, 0x00, 0x00, // mov eax, 5 (SYS_WAIT)
    0x0F, 0x05,                   // syscall
    0x48, 0x83, 0xC4, 0x08,       // add rsp, 8
    0x5F,                         // pop rdi (Restore PID to EDI)

    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                    // syscall
};

static uint8_t fork_stub_bytes[] = {
    0xB8, 0x04, 0x00, 0x00, 0x00, // mov eax, 4 (SYS_FORK)
    0x0F, 0x05,                   // syscall
    0x83, 0xF8, 0x00,             // cmp eax, 0
    0x74, 0x34,                   // je child (offset 0x34)
    0x7C, 0x3E,                   // jl error (offset 0x3E)

    // Parent
    0x48, 0x83, 0xEC, 0x08,       // sub rsp, 8
    0x48, 0x89, 0xE7,             // mov rdi, rsp
    0xB8, 0x05, 0x00, 0x00, 0x00, // mov eax, 5 (SYS_WAIT)
    0x0F, 0x05,                   // syscall

    0x8B, 0x3C, 0x24,       // mov edi, [rsp]
    0x48, 0x83, 0xC4, 0x08, // add rsp, 8

    0x83, 0xFF, 0x64, // cmp edi, 100
    0x75, 0x0C,       // jne parent_fail (offset 0x0C)

    // Success (Parent)
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0xC8, 0x00, 0x00, 0x00, // mov edi, 200
    0x0F, 0x05,                   // syscall

    // Parent Fail
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05,                   // syscall

    // Child
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x64, 0x00, 0x00, 0x00, // mov edi, 100
    0x0F, 0x05,                   // syscall

    // Error
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x02, 0x00, 0x00, 0x00, // mov edi, 2
    0x0F, 0x05                    // syscall
};

static uint8_t sbrk_stub_bytes[] = {
    0xB8, 0x09, 0x00, 0x00, 0x00, // mov eax, 9
    0x48, 0x31, 0xFF,             // xor rdi, rdi
    0x0F, 0x05,                   // syscall
    0x49, 0x89, 0xC4,             // mov r12, rax
    0xB8, 0x09, 0x00, 0x00, 0x00, // mov eax, 9
    0xBF, 0x00, 0x10, 0x00, 0x00, // mov edi, 4096
    0x0F, 0x05,                   // syscall
    0x4C, 0x39, 0xE0,             // cmp rax, r12
    0x75, 0x0C,                   // jne error (+12)
    0xC6, 0x00, 0xAA,             // mov byte ptr [rax], 0xAA
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0x31, 0xFF,                   // xor edi, edi
    0x0F, 0x05,                   // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t file_io_stub_bytes[] = {
    0xB8, 0x0A, 0x00, 0x00, 0x00,             // mov eax, SYS_OPEN
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100 (path)
    0xBE, 0x01, 0x06, 0x00, 0x00,             // mov esi, O_WRONLY | O_CREATE | O_TRUNC (0x601)
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x7C, 0x36,                               // jl error_open
    0x41, 0x89, 0xC4,                         // mov r12d, eax
    0xB8, 0x00, 0x00, 0x00, 0x00,             // mov eax, SYS_WRITE
    0x44, 0x89, 0xE7,                         // mov edi, r12d
    0x48, 0xC7, 0xC6, 0x00, 0x02, 0x40, 0x00, // mov rsi, 0x400200
    0xBA, 0x0C, 0x00, 0x00, 0x00,             // mov edx, 12
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x0C,                         // cmp eax, 12
    0x75, 0x24,                               // jne error_write
    0xB8, 0x0B, 0x00, 0x00, 0x00,             // mov eax, SYS_CLOSE
    0x44, 0x89, 0xE7,                         // mov edi, r12d
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x75, 0x21,                               // jne error_close
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, SYS_EXIT
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05,                               // syscall
    // error_open:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, SYS_EXIT
    0xBF, 0x0B, 0x00, 0x00, 0x00, // mov edi, 11
    0x0F, 0x05,                   // syscall
    // error_write:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, SYS_EXIT
    0x89, 0xC7,                   // mov edi, eax (return value from write)
    0x0F, 0x05,                   // syscall
    // error_close:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, SYS_EXIT
    0xBF, 0x0D, 0x00, 0x00, 0x00, // mov edi, 13
    0x0F, 0x05                    // syscall
};

static uint8_t chdir_stub_bytes[] = {
    0xB8, 0x0D, 0x00, 0x00, 0x00,             // mov eax, 13
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x75, 0x07,                               // jne error (+7)
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05,                               // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t sleep_stub_bytes[] = {
    0xB8, 0x0E, 0x00, 0x00, 0x00,             // mov eax, 14
    0x48, 0xC7, 0xC7, 0x0A, 0x00, 0x00, 0x00, // mov rdi, 10
    0x0F, 0x05,                               // syscall
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05                                // syscall
};

static uint8_t execve_stub_bytes[] = {
    0xB8, 0x04, 0x00, 0x00, 0x00,             // mov eax, 4 (FORK)
    0x0F, 0x05,                               // syscall
    0x83, 0xF8, 0x00,                         // cmp eax, 0
    0x74, 0x31,                               // je child (+49)
    0xB8, 0x05, 0x00, 0x00, 0x00,             // mov eax, 5 (WAIT)
    0x48, 0xC7, 0xC7, 0x00, 0x02, 0x40, 0x00, // mov rdi, 0x400200
    0x0F, 0x05,                               // syscall
    0x8B, 0x04, 0x25, 0x00, 0x02, 0x40, 0x00, // mov eax, [0x400200]
    0x3D, 0x7B, 0x00, 0x00, 0x00,             // cmp eax, 123
    0x75, 0x09,                               // jne error (+9)
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0x31, 0xFF,                               // xor edi, edi
    0x0F, 0x05,                               // syscall
    // error:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05,                   // syscall
    // child:
    0xB8, 0x02, 0x00, 0x00, 0x00,             // mov eax, 2 (EXECVE)
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100 (path)
    0x48, 0xC7, 0xC6, 0x80, 0x01, 0x40, 0x00, // mov rsi, 0x400180 (argv)
    0x48, 0x31, 0xD2,                         // xor rdx, rdx (envp = nullptr)
    0x0F, 0x05,                               // syscall
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3
    0xBF, 0x02, 0x00, 0x00, 0x00,             // mov edi, 2
    0x0F, 0x05                                // syscall
};

static uint8_t mknod_stub_bytes[] = {
    0xB8, 0x0F, 0x00, 0x00, 0x00,             // mov eax, 15 (SYS_MKNOD)
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00, // mov rdi, 0x400100 (path)
    0xBE, 0x01, 0x00, 0x00, 0x00,             // mov esi, 1 (VFS_FILE)
    0xBA, 0x00, 0x00, 0x00, 0x00,             // mov edx, 0 (dev)
    0x0F, 0x05,                               // syscall
    0x89, 0xC7,                               // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,             // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                                // syscall
};

static uint8_t pipe_bad_ptr_noncanonical_stub_bytes[] = {
    0xB8, 0x1B, 0x00, 0x00, 0x00,                               // mov eax, 27 (SYS_PIPE)
    0x48, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, // mov rdi, 0x0000800000000000
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                                                  // syscall
};

static uint8_t pipe_bad_ptr_unmapped_stub_bytes[] = {
    0xB8, 0x1B, 0x00, 0x00, 0x00,                               // mov eax, 27 (SYS_PIPE)
    0x48, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, // mov rdi, 0x0000100000000000
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                                                  // syscall
};

static uint8_t open_bad_ptr_noncanonical_stub_bytes[] = {
    0xB8, 0x0A, 0x00, 0x00, 0x00,                               // mov eax, 10 (SYS_OPEN)
    0x48, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, // mov rdi, 0x0000800000000000
    0xBE, 0x00, 0x00, 0x00, 0x00,                               // mov esi, 0
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                                                  // syscall
};

static uint8_t open_bad_ptr_unmapped_stub_bytes[] = {
    0xB8, 0x0A, 0x00, 0x00, 0x00,                               // mov eax, 10 (SYS_OPEN)
    0x48, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, // mov rdi, 0x0000100000000000
    0xBE, 0x00, 0x00, 0x00, 0x00,                               // mov esi, 0
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05                                                  // syscall
};

static uint8_t ioctl_bad_ptr_noncanonical_stub_bytes[] = {
    0xB8, 0x0A, 0x00, 0x00, 0x00,                               // mov eax, 10 (SYS_OPEN)
    0x48, 0xC7, 0xC7, 0x00, 0x01, 0x40, 0x00,                   // mov rdi, 0x400100 (path)
    0xBE, 0x00, 0x00, 0x00, 0x00,                               // mov esi, 0
    0x0F, 0x05,                                                 // syscall
    0x83, 0xF8, 0x00,                                           // cmp eax, 0
    0x7C, 0x25,                                                 // jl error_open (+37)
    0x41, 0x89, 0xC4,                                           // mov r12d, eax
    0xB8, 0x10, 0x00, 0x00, 0x00,                               // mov eax, 16 (SYS_IOCTL)
    0x44, 0x89, 0xE7,                                           // mov edi, r12d
    0xBE, 0x13, 0x54, 0x00, 0x00,                               // mov esi, 0x5413 (TIOCGWINSZ)
    0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, // mov rdx, 0x0000800000000000
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05,                                                 // syscall
    // error_open:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t bind_bad_ptr_unmapped_stub_bytes[] = {
    0xB8, 0x21, 0x00, 0x00, 0x00,                               // mov eax, 33 (SYS_SOCKET)
    0xBF, AF_INET, 0x00, 0x00, 0x00,                            // mov edi, AF_INET
    0xBE, 0x02, 0x00, 0x00, 0x00,                               // mov esi, 2 (SOCK_DGRAM)
    0xBA, 0x11, 0x00, 0x00, 0x00,                               // mov edx, 17 (IPPROTO_UDP)
    0x0F, 0x05,                                                 // syscall
    0x83, 0xF8, 0x00,                                           // cmp eax, 0
    0x7C, 0x25,                                                 // jl error_socket (+37)
    0x41, 0x89, 0xC4,                                           // mov r12d, eax
    0xB8, 0x22, 0x00, 0x00, 0x00,                               // mov eax, 34 (SYS_BIND)
    0x44, 0x89, 0xE7,                                           // mov edi, r12d
    0x48, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, // mov rsi, 0x0000100000000000
    0xBA, 0x10, 0x00, 0x00, 0x00,                               // mov edx, 16
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05,                                                 // syscall
    // error_socket:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static uint8_t sendto_bad_buf_unmapped_stub_bytes[] = {
    0xB8, 0x21, 0x00, 0x00, 0x00,                               // mov eax, 33 (SYS_SOCKET)
    0xBF, AF_INET, 0x00, 0x00, 0x00,                            // mov edi, AF_INET
    0xBE, 0x02, 0x00, 0x00, 0x00,                               // mov esi, 2 (SOCK_DGRAM)
    0xBA, 0x11, 0x00, 0x00, 0x00,                               // mov edx, 17 (IPPROTO_UDP)
    0x0F, 0x05,                                                 // syscall
    0x83, 0xF8, 0x00,                                           // cmp eax, 0
    0x7C, 0x3B,                                                 // jl error_socket (+59)
    0x41, 0x89, 0xC4,                                           // mov r12d, eax
    0xB8, 0x23, 0x00, 0x00, 0x00,                               // mov eax, 35 (SYS_SENDTO)
    0x44, 0x89, 0xE7,                                           // mov edi, r12d
    0x48, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, // mov rsi, 0x0000100000000000
    0xBA, 0x04, 0x00, 0x00, 0x00,                               // mov edx, 4
    0x41, 0xBA, 0x00, 0x00, 0x00, 0x00,                         // mov r10d, 0
    0x49, 0xB8, 0x00, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, // mov r8, 0x400200
    0x41, 0xB9, 0x10, 0x00, 0x00, 0x00,                         // mov r9d, 16
    0x0F, 0x05,                                                 // syscall
    0x89, 0xC7,                                                 // mov edi, eax
    0xB8, 0x03, 0x00, 0x00, 0x00,                               // mov eax, 3 (SYS_EXIT)
    0x0F, 0x05,                                                 // syscall
    // error_socket:
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x01, 0x00, 0x00, 0x00, // mov edi, 1
    0x0F, 0x05                    // syscall
};

static void syscall_test_exit_handler(int code)
{
    if (test_runner_pid != 0 && current_process->pid != test_runner_pid) {
        return;
    }
    test_exit_code = code;
    // We caught the sys_exit from user mode!
    // Return to the test function stack
    syscall_test_prepare_longjmp();
    __builtin_longjmp(test_env, 1);
}

TEST(test_syscall_write_exit)
{
    test_runner_pid = current_process->pid;
    // Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page) {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, write_exit_stub_bytes, sizeof(write_exit_stub_bytes));

    // Copy the string to 0x400100
    const char *msg = "Hello Write\n";
    memcpy((void *)((uint64_t)virt_page + 0x100), msg, strlen(msg) + 1);

    // Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // Prepare for jump
    if (__builtin_setjmp(test_env) == 0) {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16; // Top of page, aligned
        enter_user_mode(user_base, user_stack);

        // Should not reach here
        printk("Syscall Test: IRETQ failed to jump\n");
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp (sys_exit handler)
        // Verify results
        bool passed = true;

        // We expect exit code 42
        if (test_exit_code != 42) {
            printk("Syscall Test: Exit code mismatch. Expected 42, got %d\n", test_exit_code);
            passed = false;
        } else {
            printk("Syscall Test: Write/Exit successful, exit code 42\n");
        }

        // Cleanup
        syscall_set_exit_hook(nullptr);

        return passed;
    }
}

TEST(test_syscall_getpid)
{
    test_runner_pid = current_process->pid;
    // Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page) {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, getpid_stub_bytes, sizeof(getpid_stub_bytes));

    // Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // Prepare for jump
    if (__builtin_setjmp(test_env) == 0) {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    } else {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect PID 1 (kernel task)
        if (test_exit_code != current_process->pid) {
            printk("Syscall Test: PID mismatch. Expected %d, got %d\n", current_process->pid, test_exit_code);
            passed = false;
        } else {
            printk("Syscall Test: GETPID successful, got %d\n", test_exit_code);
        }

        // Cleanup
        syscall_set_exit_hook(nullptr);

        return passed;
    }
}

TEST(test_syscall_yield)
{
    test_runner_pid = current_process->pid;
    // Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page) {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, yield_stub_bytes, sizeof(yield_stub_bytes));

    // Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // Prepare for jump
    if (__builtin_setjmp(test_env) == 0) {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    } else {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect exit code 0
        if (test_exit_code != 0) {
            printk("Syscall Test: Yield failed or invalid exit code. Got %d\n", test_exit_code);
            passed = false;
        } else {
            printk("Syscall Test: YIELD successful\n");
        }

        // Cleanup
        syscall_set_exit_hook(nullptr);

        return passed;
    }
}

TEST(test_syscall_spawn)
{
    test_runner_pid = current_process->pid;
    // Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page) {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, spawn_stub_bytes, sizeof(spawn_stub_bytes));

    // Copy the path string to 0x400100
    const char *path = "/bin/prog";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    // Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // Prepare for jump
    if (__builtin_setjmp(test_env) == 0) {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    } else {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect a valid PID (> 1, since 1 is kernel)
        if (test_exit_code <= 1) {
            printk("Syscall Test: Spawn failed or invalid PID. Got %d\n", test_exit_code);
            passed = false;
        } else {
            printk("Syscall Test: SPAWN successful, new PID %d\n", test_exit_code);
        }

        // Cleanup
        syscall_set_exit_hook(nullptr);

        return passed;
    }
}

TEST(test_syscall_fork)
{
    test_runner_pid = current_process->pid;
    // Allocate a page for user code and stack
    void *phys_page = pmm_alloc_page();
    if (!phys_page) {
        printk("Syscall Test: Failed to alloc page\n");
        return false;
    }

    // Map it as User | Present | RW at 0x400000
    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));

    uint64_t hhdm_offset = 0xffff800000000000;

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Copy stub to the page
    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, fork_stub_bytes, sizeof(fork_stub_bytes));

    // Register exit hook
    syscall_set_exit_hook(syscall_test_exit_handler);

    // Prepare for jump
    if (__builtin_setjmp(test_env) == 0) {
        // Switch to User Mode
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);

        return false;
    } else {
        syscall_test_resume_after_longjmp();
        // Returned from longjmp
        bool passed = true;

        // We expect exit code 200 (Parent success)
        if (test_exit_code != 200) {
            printk("Syscall Test: Fork failed. Exit code %d\n", test_exit_code);
            passed = false;
        } else {
            printk("Syscall Test: FORK successful\n");
        }

        // Cleanup
        syscall_set_exit_hook(nullptr);

        return passed;
    }
}

TEST(test_syscall_sbrk)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // Initialize heap_end for sbrk test
    current_process->heap_end = 0x500000;

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, sbrk_stub_bytes, sizeof(sbrk_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: SBRK successful\n");
        else
            printk("Syscall Test: SBRK failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_file_io)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);


    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, file_io_stub_bytes, sizeof(file_io_stub_bytes));

    const char *path = "/mnt/FILEIO.TXT";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);
    const char *data = "Hello FileIO";
    memcpy((void *)((uint64_t)virt_page + 0x200), data, strlen(data) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: FileIO successful\n");
        else
            printk("Syscall Test: FileIO failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_open_flags)
{
    const char *path = "/mnt/flag_test.txt";
    char buf[16]     = {0};

    // Create and write.
    int fd = sys_open(path, O_CREATE | O_WRONLY | O_TRUNC);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_write(fd, "abc", 3) == 3);
    TEST_ASSERT(sys_close(fd) == 0);

    // Read back.
    fd = sys_open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == 3);
    TEST_ASSERT(strncmp(buf, "abc", 3) == 0);
    TEST_ASSERT(sys_close(fd) == 0);

    // Truncate and verify empty.
    fd = sys_open(path, O_WRONLY | O_TRUNC);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_close(fd) == 0);
    fd = sys_open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == 0);
    TEST_ASSERT(sys_close(fd) == 0);

    // Append and verify.
    fd = sys_open(path, O_CREATE | O_WRONLY | O_TRUNC);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_write(fd, "one", 3) == 3);
    TEST_ASSERT(sys_close(fd) == 0);
    fd = sys_open(path, O_WRONLY | O_APPEND);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_write(fd, "two", 3) == 3);
    TEST_ASSERT(sys_close(fd) == 0);
    fd = sys_open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0);
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == 6);
    TEST_ASSERT(strncmp(buf, "onetwo", 6) == 0);
    TEST_ASSERT(sys_close(fd) == 0);

    // Read on write-only should fail.
    fd = sys_open(path, O_WRONLY);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == -EBADF);
    TEST_ASSERT(sys_close(fd) == 0);
    return true;
}

TEST(test_syscall_chdir)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, chdir_stub_bytes, sizeof(chdir_stub_bytes));

    const char *path = "/";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: CHDIR successful\n");
        else
            printk("Syscall Test: CHDIR failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_getcwd)
{
    char buf[64] = {0};
    TEST_ASSERT(sys_getcwd(buf, sizeof(buf)) == 0);
    TEST_ASSERT(strcmp(buf, "/") == 0);

    char saved[sizeof(current_process->cwd)];
    strncpy(saved, current_process->cwd, sizeof(saved) - 1);
    saved[sizeof(saved) - 1] = '\0';

    strncpy(current_process->cwd, "/tmp", sizeof(current_process->cwd) - 1);
    current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT(sys_getcwd(buf, sizeof(buf)) == 0);
    TEST_ASSERT(strcmp(buf, "/tmp") == 0);

    strncpy(current_process->cwd, saved, sizeof(current_process->cwd) - 1);
    current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';

    return true;
}

TEST(test_syscall_open_create_fat32)
{
    const char *path = "/mnt/SYS_TOUCH.TST";

    int fd = sys_open(path, O_CREATE | O_WRONLY | O_TRUNC);
    TEST_ASSERT(fd >= 0);

    const char *payload = "sys_touch_payload";
    TEST_ASSERT(sys_write(fd, payload, strlen(payload)) == (int)strlen(payload));
    TEST_ASSERT(sys_close(fd) == 0);

    fd = sys_open(path, O_RDONLY);
    TEST_ASSERT(fd >= 0);
    char buf[32] = {0};
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == (int)strlen(payload));
    TEST_ASSERT(strncmp(buf, payload, strlen(payload)) == 0);
    TEST_ASSERT(sys_close(fd) == 0);

    TEST_ASSERT(sys_unlink(path) == 0);
    return true;
}

TEST(test_syscall_sleep)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, sleep_stub_bytes, sizeof(sleep_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: SLEEP successful\n");
        else
            printk("Syscall Test: SLEEP failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_execve)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    uint64_t hhdm_offset = 0xffff800000000000;


    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);


    void *virt_page = (void *)((uint64_t)phys_page + hhdm_offset);
    memcpy(virt_page, execve_stub_bytes, sizeof(execve_stub_bytes));

    const char *path = "/bin/prog";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);
    uint64_t *argv_area      = (uint64_t *)((uint64_t)virt_page + 0x180);
    argv_area[0]             = 0x400100;
    argv_area[1]             = 0;
    uint32_t expected_status = 123;
    memcpy((void *)((uint64_t)virt_page + 0x200), &expected_status, sizeof(uint32_t));

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed)
            printk("Syscall Test: EXECVE successful\n");
        else
            printk("Syscall Test: EXECVE failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST_PRIO(test_syscall_mknod, 10)
{
    uint64_t user_base = 0x400000;
    test_runner_pid    = current_process->pid;
    void *phys_page    = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    cr3 &= ~0xFFF; // Mask flags

    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, mknod_stub_bytes, sizeof(mknod_stub_bytes));

    const char *path = "/dev_test";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        bool passed = (test_exit_code == 0);
        if (passed) {
            vfs_inode_t *node = vfs_resolve_path("/dev_test");
            if (node && (node->flags & VFS_FILE)) {
                printk("Syscall Test: MKNOD successful (node found)\n");
            } else {
                printk("Syscall Test: MKNOD failed (node not found or wrong type)\n");
                passed = false;
            }
            if (node)
                vfs_release(node);
        } else
            printk("Syscall Test: MKNOD failed, exit code %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_user_ptr_noncanonical)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, pipe_bad_ptr_noncanonical_stub_bytes, sizeof(pipe_bad_ptr_noncanonical_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -1);
        if (!passed)
            printk("Syscall Test: Noncanonical ptr expected -1, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_user_ptr_unmapped)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, pipe_bad_ptr_unmapped_stub_bytes, sizeof(pipe_bad_ptr_unmapped_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -1);
        if (!passed)
            printk("Syscall Test: Unmapped ptr expected -1, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_path_ptr_noncanonical)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, open_bad_ptr_noncanonical_stub_bytes, sizeof(open_bad_ptr_noncanonical_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -EFAULT);
        if (!passed)
            printk("Syscall Test: Noncanonical path expected -EFAULT, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_path_ptr_unmapped)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, open_bad_ptr_unmapped_stub_bytes, sizeof(open_bad_ptr_unmapped_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -EFAULT);
        if (!passed)
            printk("Syscall Test: Unmapped path expected -EFAULT, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_ioctl_ptr_noncanonical)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, ioctl_bad_ptr_noncanonical_stub_bytes, sizeof(ioctl_bad_ptr_noncanonical_stub_bytes));

    const char *path = "/dev/console";
    memcpy((void *)((uint64_t)virt_page + 0x100), path, strlen(path) + 1);

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -EFAULT);
        if (!passed)
            printk("Syscall Test: ioctl noncanonical expected -EFAULT, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_bind_ptr_unmapped)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, bind_bad_ptr_unmapped_stub_bytes, sizeof(bind_bad_ptr_unmapped_stub_bytes));

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -EFAULT);
        if (!passed)
            printk("Syscall Test: bind unmapped expected -EFAULT, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_sendto_buf_ptr_unmapped)
{
    test_runner_pid = current_process->pid;
    void *phys_page = pmm_alloc_page();
    if (!phys_page)
        return false;

    uint64_t user_base = 0x400000;
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    vmm_map_page((pml4_t)cr3, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void *virt_page = (void *)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, sendto_bad_buf_unmapped_stub_bytes, sizeof(sendto_bad_buf_unmapped_stub_bytes));

    struct sockaddr_in dest = {0};
    dest.sin_family         = AF_INET;
    dest.sin_port           = 0x1234;
    dest.sin_addr[0]        = 1;
    dest.sin_addr[1]        = 2;
    dest.sin_addr[2]        = 3;
    dest.sin_addr[3]        = 4;
    memcpy((void *)((uint64_t)virt_page + 0x200), &dest, sizeof(dest));

    syscall_set_exit_hook(syscall_test_exit_handler);
    thread_t *t            = current_thread;
    const bool old_is_user = t ? t->is_user : false;
    if (t)
        t->is_user = true;
    test_exit_code = 0;

    if (__builtin_setjmp(test_env) == 0) {
        uint64_t user_stack = user_base + 4096 - 16;
        enter_user_mode(user_base, user_stack);
        if (t)
            t->is_user = old_is_user;
        syscall_set_exit_hook(nullptr);
        return false;
    } else {
        syscall_test_resume_after_longjmp();
        if (t)
            t->is_user = old_is_user;
        bool passed = (test_exit_code == -EFAULT);
        if (!passed)
            printk("Syscall Test: sendto unmapped expected -EFAULT, got %d\n", test_exit_code);
        syscall_set_exit_hook(nullptr);
        return passed;
    }
}

TEST(test_syscall_invalid_paths_and_fds)
{
    // Empty path rejected.
    TEST_ASSERT(sys_open("", 0) == -EBADPATH);
    TEST_ASSERT(sys_chdir("") == -EBADPATH);

    // chdir to non-directory should fail.
    TEST_ASSERT(sys_chdir("/bin/init") == -ENOTDIR);

    char buf[8];
    int fd = sys_open("/bin/init", 0);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_close(fd) == 0);

    // Read on closed/invalid descriptors should return EBADF.
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == -EBADF);
    TEST_ASSERT(sys_read(42, buf, sizeof(buf)) == -EBADF);
    TEST_ASSERT(sys_close(fd) == -EBADF);
    return true;
}

// Pipe syscall declaration
TEST(test_syscall_pipe_basic)
{
    int pipefd[2] = {-1, -1};

    // Create a pipe
    TEST_ASSERT(sys_pipe(pipefd) == 0);
    TEST_ASSERT(pipefd[0] >= 3); // Read end
    TEST_ASSERT(pipefd[1] >= 3); // Write end
    TEST_ASSERT(pipefd[0] != pipefd[1]);

    // Write to pipe
    const char *msg = "Hello, pipe!";
    int written     = sys_write(pipefd[1], msg, strlen(msg));
    TEST_ASSERT(written == (int)strlen(msg));

    // Read from pipe
    char buf[32]   = {0};
    int bytes_read = sys_read(pipefd[0], buf, sizeof(buf) - 1);
    TEST_ASSERT(bytes_read == (int)strlen(msg));
    TEST_ASSERT(strncmp(buf, msg, strlen(msg)) == 0);

    // Close both ends
    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    TEST_ASSERT(sys_close(pipefd[1]) == 0);

    return true;
}

TEST(test_syscall_pipe_multiple_writes)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    // Multiple writes
    for (int i = 0; i < 3; i++) {
        const char *msgs[] = {"one", "two", "three"};
        int written        = sys_write(pipefd[1], msgs[i], strlen(msgs[i]));
        TEST_ASSERT(written == (int)strlen(msgs[i]));
    }

    // Read all data
    char buf[32] = {0};
    int total    = sys_read(pipefd[0], buf, sizeof(buf) - 1);
    TEST_ASSERT(total == 11); // "one" + "two" + "three" = 3+3+5=11
    TEST_ASSERT(strncmp(buf, "onetwothree", 11) == 0);

    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    TEST_ASSERT(sys_close(pipefd[1]) == 0);
    return true;
}

TEST(test_syscall_pipe_null_arg)
{
    // nullptr argument should fail
    TEST_ASSERT(sys_pipe(nullptr) == -1);
    return true;
}

// lseek and dup syscall declarations
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

TEST(test_syscall_lseek_basic)
{
    // Create a test file
    int fd = sys_open("/lseek_test.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    // Write some data
    const char *data = "0123456789ABCDEF";
    int written      = sys_write(fd, data, 16);
    TEST_ASSERT(written == 16);

    // Seek to beginning
    long pos = sys_lseek(fd, 0, SEEK_SET);
    TEST_ASSERT(pos == 0);

    // Read back
    char buf[8]    = {0};
    int bytes_read = sys_read(fd, buf, 4);
    TEST_ASSERT(bytes_read == 4);
    TEST_ASSERT(strncmp(buf, "0123", 4) == 0);

    // Seek relative from current position
    pos = sys_lseek(fd, 4, SEEK_CUR);
    TEST_ASSERT(pos == 8); // was at 4, moved 4 more

    // Read from new position
    memset(buf, 0, sizeof(buf));
    bytes_read = sys_read(fd, buf, 4);
    TEST_ASSERT(bytes_read == 4);
    TEST_ASSERT(strncmp(buf, "89AB", 4) == 0);

    // Seek from end
    pos = sys_lseek(fd, -4, SEEK_END);
    TEST_ASSERT(pos == 12);

    memset(buf, 0, sizeof(buf));
    bytes_read = sys_read(fd, buf, 4);
    TEST_ASSERT(bytes_read == 4);
    TEST_ASSERT(strncmp(buf, "CDEF", 4) == 0);

    sys_close(fd);
    sys_unlink("/lseek_test.txt");
    return true;
}

TEST(test_syscall_lseek_invalid)
{
    // Invalid fd
    TEST_ASSERT(sys_lseek(-1, 0, SEEK_SET) == -EBADF);
    TEST_ASSERT(sys_lseek(999, 0, SEEK_SET) == -EBADF);

    // Invalid whence
    int fd = sys_open("/lseek_invalid.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    TEST_ASSERT(sys_lseek(fd, 0, 99) == -EINVAL);

    // Seek before beginning should fail
    TEST_ASSERT(sys_lseek(fd, -1, SEEK_SET) == -EINVAL);

    sys_close(fd);
    sys_unlink("/lseek_invalid.txt");
    return true;
}

TEST(test_syscall_lseek_pipe_fails)
{
    // Pipes are not seekable
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    TEST_ASSERT(sys_lseek(pipefd[0], 0, SEEK_SET) == -ENOTSUP);
    TEST_ASSERT(sys_lseek(pipefd[1], 0, SEEK_SET) == -ENOTSUP);

    sys_close(pipefd[0]);
    sys_close(pipefd[1]);
    return true;
}

TEST(test_syscall_dup_basic)
{
    // Create a test file
    int fd1 = sys_open("/dup_test.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd1 >= 3);

    // Write some data
    const char *msg = "Hello, dup!";
    sys_write(fd1, msg, strlen(msg));

    // Seek back to start
    sys_lseek(fd1, 0, SEEK_SET);

    // Duplicate the fd
    int fd2 = sys_dup(fd1);
    TEST_ASSERT(fd2 >= 0); // dup returns lowest available fd
    TEST_ASSERT(fd2 != fd1);

    // Read from original fd
    char buf1[16] = {0};
    int read1     = sys_read(fd1, buf1, 6);
    TEST_ASSERT(read1 == 6);
    TEST_ASSERT(strncmp(buf1, "Hello,", 6) == 0);

    // Read from duplicated fd should continue from same position
    // (they share the same offset)
    char buf2[16] = {0};
    int read2     = sys_read(fd2, buf2, 5);
    TEST_ASSERT(read2 == 5);
    TEST_ASSERT(strncmp(buf2, " dup!", 5) == 0);

    // Close both fds
    TEST_ASSERT(sys_close(fd1) == 0);
    TEST_ASSERT(sys_close(fd2) == 0);

    sys_unlink("/dup_test.txt");
    return true;
}

TEST(test_syscall_dup_invalid)
{
    // Invalid fd
    TEST_ASSERT(sys_dup(-1) == -EBADF);
    TEST_ASSERT(sys_dup(999) == -EBADF);

    int fd = sys_open("/dup_invalid.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    TEST_ASSERT(sys_close(fd) == 0);
    TEST_ASSERT(sys_dup(fd) == -EBADF);
    sys_unlink("/dup_invalid.txt");

    return true;
}

TEST(test_syscall_dup_ref_counting)
{
    // Test that closing one dup'd fd doesn't affect the other
    int fd1 = sys_open("/dup_ref.txt", O_CREATE | O_RDWR | O_TRUNC);
    if (fd1 < 3)
        return false;

    int written = sys_write(fd1, "test data", 9);
    if (written != 9) {
        sys_close(fd1);
        return false;
    }

    long pos = sys_lseek(fd1, 0, SEEK_SET);
    if (pos != 0) {
        sys_close(fd1);
        return false;
    }

    int fd2 = sys_dup(fd1);
    if (fd2 < 0) {
        sys_close(fd1);
        return false;
    }

    // Close original
    if (sys_close(fd1) != 0) {
        sys_close(fd2);
        return false;
    }

    // fd2 should still work
    char buf[16] = {0};
    int bytes    = sys_read(fd2, buf, 9);
    if (bytes != 9) {
        sys_close(fd2);
        return false;
    }
    if (strncmp(buf, "test data", 9) != 0) {
        sys_close(fd2);
        return false;
    }

    sys_close(fd2);
    sys_unlink("/dup_ref.txt");
    return true;
}

TEST(test_syscall_stat_basic)
{
    // Create a test file with known content
    int fd = sys_open("/stat_test.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    const char *content = "Hello, stat!";
    int written         = sys_write(fd, content, strlen(content));
    TEST_ASSERT(written == (int)strlen(content));
    sys_close(fd);

    // Now stat the file
    stat_t st = {0};
    int rc    = sys_stat("/stat_test.txt", &st);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(st.size == strlen(content));
    TEST_ASSERT(st.type == VFS_FILE);
    TEST_ASSERT(S_ISREG(st.st_mode));

    sys_unlink("/stat_test.txt");
    return true;
}

TEST(test_syscall_stat_directory)
{
    stat_t st = {0};
    int rc    = sys_stat("/", &st);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(st.type == VFS_DIRECTORY);
    TEST_ASSERT(S_ISDIR(st.st_mode));
    return true;
}

TEST(test_syscall_stat_nonexistent)
{
    stat_t st = {0};
    int rc    = sys_stat("/nonexistent_file_xyz.txt", &st);
    TEST_ASSERT(rc == -ENOENT);
    return true;
}

TEST(test_syscall_fstat_basic)
{
    int fd = sys_open("/fstat_test.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    const char *content = "fstat test data";
    sys_write(fd, content, strlen(content));

    stat_t st = {0};
    int rc    = sys_fstat(fd, &st);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(st.size == strlen(content));
    TEST_ASSERT(st.type == VFS_FILE);
    TEST_ASSERT(S_ISREG(st.st_mode));

    sys_close(fd);
    sys_unlink("/fstat_test.txt");
    return true;
}

TEST(test_syscall_fstat_invalid_fd)
{
    stat_t st = {0};
    TEST_ASSERT(sys_fstat(-1, &st) == -EBADF);
    TEST_ASSERT(sys_fstat(999, &st) == -EBADF);
    return true;
}

TEST(test_syscall_ftruncate_zero)
{
    int fd = sys_open("/ftruncate_zero.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    const char *content = "truncate me";
    TEST_ASSERT(sys_write(fd, content, strlen(content)) == (int)strlen(content));

    stat_t st = {0};
    TEST_ASSERT(sys_fstat(fd, &st) == 0);
    TEST_ASSERT(st.size == strlen(content));

    TEST_ASSERT(sys_ftruncate(fd, 0) == 0);
    TEST_ASSERT(sys_fstat(fd, &st) == 0);
    TEST_ASSERT(st.size == 0);

    TEST_ASSERT(sys_lseek(fd, 0, SEEK_SET) == 0);
    char buf[8] = {0};
    TEST_ASSERT(sys_read(fd, buf, sizeof(buf)) == 0);

    TEST_ASSERT(sys_close(fd) == 0);
    TEST_ASSERT(sys_unlink("/ftruncate_zero.txt") == 0);
    return true;
}

TEST(test_syscall_ftruncate_invalid)
{
    TEST_ASSERT(sys_ftruncate(-1, 0) == -EBADF);
    TEST_ASSERT(sys_ftruncate(999, 0) == -EBADF);

    int fd = sys_open("/ftruncate_invalid.txt", O_CREATE | O_RDONLY);
    TEST_ASSERT(fd >= 3);
    TEST_ASSERT(sys_ftruncate(fd, 0) == -EBADF);
    TEST_ASSERT(sys_ftruncate(fd, -1) == -EINVAL);
    TEST_ASSERT(sys_close(fd) == 0);
    TEST_ASSERT(sys_unlink("/ftruncate_invalid.txt") == 0);

    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);
    TEST_ASSERT(sys_ftruncate(pipefd[1], 0) == -ENOTSUP);
    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    TEST_ASSERT(sys_close(pipefd[1]) == 0);
    return true;
}

TEST(test_syscall_ftruncate_nonzero_unsupported)
{
    int fd = sys_open("/ftruncate_nonzero.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    const char *content = "1234567890";
    TEST_ASSERT(sys_write(fd, content, strlen(content)) == (int)strlen(content));
    TEST_ASSERT(sys_ftruncate(fd, 4) == -ENOTSUP);

    stat_t st = {0};
    TEST_ASSERT(sys_fstat(fd, &st) == 0);
    TEST_ASSERT(st.size == strlen(content));

    TEST_ASSERT(sys_close(fd) == 0);
    TEST_ASSERT(sys_unlink("/ftruncate_nonzero.txt") == 0);
    return true;
}

TEST(test_syscall_fcntl_get_setfl)
{
    int fd = sys_open("/fcntl_flags.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    int flags = sys_fcntl(fd, F_GETFL, 0);
    TEST_ASSERT(flags >= 0);
    TEST_ASSERT((flags & O_RDWR) == O_RDWR);
    TEST_ASSERT((flags & O_NONBLOCK) == 0);
    TEST_ASSERT((flags & O_APPEND) == 0);

    TEST_ASSERT(sys_fcntl(fd, F_SETFL, O_NONBLOCK | O_APPEND) == 0);
    flags = sys_fcntl(fd, F_GETFL, 0);
    TEST_ASSERT((flags & O_RDWR) == O_RDWR);
    TEST_ASSERT((flags & O_NONBLOCK) != 0);
    TEST_ASSERT((flags & O_APPEND) != 0);

    TEST_ASSERT(sys_fcntl(fd, F_SETFL, 0) == 0);
    flags = sys_fcntl(fd, F_GETFL, 0);
    TEST_ASSERT((flags & O_RDWR) == O_RDWR);
    TEST_ASSERT((flags & O_NONBLOCK) == 0);
    TEST_ASSERT((flags & O_APPEND) == 0);

    TEST_ASSERT(sys_close(fd) == 0);
    TEST_ASSERT(sys_unlink("/fcntl_flags.txt") == 0);
    return true;
}

TEST(test_syscall_fcntl_invalid)
{
    TEST_ASSERT(sys_fcntl(-1, F_GETFL, 0) == -EBADF);
    TEST_ASSERT(sys_fcntl(999, F_GETFL, 0) == -EBADF);

    int fd = sys_open("/fcntl_invalid.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);

    TEST_ASSERT(sys_fcntl(fd, 999, 0) == -EINVAL);
    TEST_ASSERT(sys_fcntl(fd, F_GETFD, 0) == 0);
    TEST_ASSERT(sys_fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
    TEST_ASSERT(sys_fcntl(fd, F_SETFD, FD_CLOEXEC | 2) == -EINVAL);

    TEST_ASSERT(sys_close(fd) == 0);
    TEST_ASSERT(sys_unlink("/fcntl_invalid.txt") == 0);
    return true;
}

TEST(test_syscall_poll_timeout)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
    uint64_t start    = scheduler_ticks;
    TEST_ASSERT(sys_poll(&pfd, 1, 50) == 0);
    uint64_t end = scheduler_ticks;
    TEST_ASSERT(end >= start);
    TEST_ASSERT(end - start >= 1);
    TEST_ASSERT(pfd.revents == 0);

    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    TEST_ASSERT(sys_close(pipefd[1]) == 0);
    return true;
}

TEST(test_syscall_poll_pipe_ready)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    struct pollfd read_pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
    TEST_ASSERT(sys_poll(&read_pfd, 1, 0) == 0);
    TEST_ASSERT(sys_write(pipefd[1], "z", 1) == 1);
    TEST_ASSERT(sys_poll(&read_pfd, 1, 0) == 1);
    TEST_ASSERT((read_pfd.revents & POLLIN) != 0);

    char c = 0;
    TEST_ASSERT(sys_read(pipefd[0], &c, 1) == 1);
    TEST_ASSERT(c == 'z');

    struct pollfd write_pfd = {.fd = pipefd[1], .events = POLLOUT, .revents = 0};
    TEST_ASSERT(sys_poll(&write_pfd, 1, 0) == 1);
    TEST_ASSERT((write_pfd.revents & POLLOUT) != 0);

    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    TEST_ASSERT(sys_poll(&write_pfd, 1, 0) == 1);
    TEST_ASSERT((write_pfd.revents & POLLERR) != 0);
    TEST_ASSERT(sys_close(pipefd[1]) == 0);
    return true;
}

TEST(test_syscall_poll_invalid_fd)
{
    struct pollfd pfd = {.fd = 999, .events = POLLIN, .revents = 0};
    TEST_ASSERT(sys_poll(&pfd, 1, 0) == 1);
    TEST_ASSERT((pfd.revents & POLLNVAL) != 0);
    return true;
}

TEST(test_syscall_link_basic)
{
    // Create a file
    int fd = sys_open("/link_src.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    sys_write(fd, "link test", 9);
    sys_close(fd);

    // Create a hard link
    int rc = sys_link("/link_src.txt", "/link_dst.txt");
    TEST_ASSERT(rc == 0);

    // Both files should have same content
    fd = sys_open("/link_dst.txt", O_RDONLY);
    TEST_ASSERT(fd >= 3);
    char buf[16] = {0};
    int bytes    = sys_read(fd, buf, 9);
    TEST_ASSERT(bytes == 9);
    TEST_ASSERT(strncmp(buf, "link test", 9) == 0);
    sys_close(fd);

    // Cleanup
    sys_unlink("/link_src.txt");
    sys_unlink("/link_dst.txt");
    return true;
}

TEST(test_syscall_link_fat32_not_supported)
{
    sys_unlink("/mnt/LINKSRC.TXT");
    sys_unlink("/mnt/LINKDST.TXT");

    int fd = sys_open("/mnt/LINKSRC.TXT", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    sys_close(fd);

    TEST_ASSERT(sys_link("/mnt/LINKSRC.TXT", "/mnt/LINKDST.TXT") == -ENOTSUP);

    sys_unlink("/mnt/LINKSRC.TXT");
    sys_unlink("/mnt/LINKDST.TXT");
    return true;
}

TEST(test_syscall_link_cross_filesystem_not_supported)
{
    sys_unlink("/mnt/LINKXFS.TXT");
    sys_unlink("/link_from_mnt.txt");

    int fd = sys_open("/mnt/LINKXFS.TXT", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    sys_close(fd);

    TEST_ASSERT(sys_link("/mnt/LINKXFS.TXT", "/link_from_mnt.txt") == -ENOTSUP);

    stat_t st = {0};
    TEST_ASSERT(sys_stat("/mnt/LINKXFS.TXT", &st) == 0);
    TEST_ASSERT(sys_stat("/link_from_mnt.txt", &st) == -ENOENT);

    sys_unlink("/mnt/LINKXFS.TXT");
    sys_unlink("/link_from_mnt.txt");
    return true;
}

TEST(test_syscall_rename_basic)
{
    sys_unlink("/rename_src.txt");
    sys_unlink("/rename_dst.txt");

    int fd = sys_open("/rename_src.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    const char *content = "rename test";
    sys_write(fd, content, strlen(content));
    sys_close(fd);

    int fd2 = sys_open("/rename_dst.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd2 >= 3);
    sys_write(fd2, "old", 3);
    sys_close(fd2);

    TEST_ASSERT(sys_rename("/rename_src.txt", "/rename_dst.txt") == 0);

    stat_t st;
    TEST_ASSERT(sys_stat("/rename_src.txt", &st) == -ENOENT);

    fd = sys_open("/rename_dst.txt", O_RDONLY);
    TEST_ASSERT(fd >= 3);
    char buf[16] = {0};
    int bytes    = sys_read(fd, buf, strlen(content));
    TEST_ASSERT(bytes == (int)strlen(content));
    TEST_ASSERT(strncmp(buf, content, strlen(content)) == 0);
    sys_close(fd);

    sys_unlink("/rename_dst.txt");
    return true;
}

TEST(test_syscall_rename_cross_filesystem_not_supported)
{
    sys_unlink("/rename_xfs_src.txt");
    sys_unlink("/mnt/RENXFS.TXT");

    int fd = sys_open("/rename_xfs_src.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    sys_write(fd, "xfs", 3);
    sys_close(fd);

    TEST_ASSERT(sys_rename("/rename_xfs_src.txt", "/mnt/RENXFS.TXT") == -ENOTSUP);

    stat_t st = {0};
    TEST_ASSERT(sys_stat("/rename_xfs_src.txt", &st) == 0);
    TEST_ASSERT(sys_stat("/mnt/RENXFS.TXT", &st) == -ENOENT);

    sys_unlink("/rename_xfs_src.txt");
    sys_unlink("/mnt/RENXFS.TXT");
    return true;
}

TEST(test_syscall_rename_fat32)
{
    sys_unlink("/mnt/REN_SRC.TXT");
    sys_unlink("/mnt/REN_DST.TXT");

    int fd = sys_open("/mnt/REN_SRC.TXT", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    const char *content = "fat32 rename";
    sys_write(fd, content, strlen(content));
    sys_close(fd);

    int fd2 = sys_open("/mnt/REN_DST.TXT", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd2 >= 3);
    sys_write(fd2, "old", 3);
    sys_close(fd2);

    TEST_ASSERT(sys_rename("/mnt/REN_SRC.TXT", "/mnt/REN_DST.TXT") == 0);

    stat_t st;
    TEST_ASSERT(sys_stat("/mnt/REN_SRC.TXT", &st) == -ENOENT);

    fd = sys_open("/mnt/REN_DST.TXT", O_RDONLY);
    TEST_ASSERT(fd >= 3);
    char buf[16] = {0};
    int bytes    = sys_read(fd, buf, strlen(content));
    TEST_ASSERT(bytes == (int)strlen(content));
    TEST_ASSERT(strncmp(buf, content, strlen(content)) == 0);
    sys_close(fd);

    sys_unlink("/mnt/REN_DST.TXT");
    return true;
}

TEST(test_syscall_unlink_basic)
{
    // Create a file
    int fd = sys_open("/unlink_test.txt", O_CREATE | O_RDWR | O_TRUNC);
    TEST_ASSERT(fd >= 3);
    sys_close(fd);

    // File should exist
    stat_t st;
    TEST_ASSERT(sys_stat("/unlink_test.txt", &st) == 0);

    // Unlink it
    TEST_ASSERT(sys_unlink("/unlink_test.txt") == 0);

    // File should no longer exist
    TEST_ASSERT(sys_stat("/unlink_test.txt", &st) == -ENOENT);
    return true;
}

TEST(test_syscall_unlink_nonexistent)
{
    TEST_ASSERT(sys_unlink("/nonexistent_xyz.txt") == -ENOENT);
    return true;
}

TEST(test_syscall_readdir_basic)
{
    int fd = sys_open("/", O_RDONLY);
    TEST_ASSERT(fd >= 3);

    vfs_dirent_t dent;
    int count = 0;
    while (sys_readdir(fd, &dent) > 0) {
        count++;
        // Each entry should have a name
        TEST_ASSERT(dent.name[0] != '\0');
    }
    // Root directory should have some entries
    TEST_ASSERT(count > 0);

    sys_close(fd);
    return true;
}

TEST(test_syscall_readdir_invalid_fd)
{
    vfs_dirent_t dent;
    TEST_ASSERT(sys_readdir(-1, &dent) == -EBADF);
    TEST_ASSERT(sys_readdir(999, &dent) == -EBADF);

    int fd = sys_open("/bin/init", O_RDONLY);
    TEST_ASSERT(fd >= 3);
    TEST_ASSERT(sys_readdir(fd, &dent) == -ENOTDIR);
    TEST_ASSERT(sys_close(fd) == 0);
    return true;
}

TEST(test_syscall_usleep_basic)
{
    // usleep should return 0 on success
    int rc = sys_usleep(1000); // 1ms
    TEST_ASSERT(rc == 0);
    return true;
}

TEST(test_syscall_usleep_zero)
{
    int rc = sys_usleep(0);
    TEST_ASSERT(rc == 0);
    return true;
}

TEST(test_syscall_mmap_anonymous)
{
    void *addr = sys_mmap(nullptr,
                          PAGE_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1,
                          0);
    TEST_ASSERT(addr != MAP_FAILED);

    volatile uint8_t *p = (volatile uint8_t *)addr;
    for (size_t i = 0; i < 16; i++)
        TEST_ASSERT(p[i] == 0);

    p[0]             = 'A';
    p[PAGE_SIZE - 1] = 'Z';
    TEST_ASSERT(p[0] == 'A');
    TEST_ASSERT(p[PAGE_SIZE - 1] == 'Z');

    int rc = sys_munmap(addr, PAGE_SIZE);
    TEST_ASSERT(rc == 0);
    return true;
}

TEST(test_syscall_mmap_prot_none)
{
    void *addr = sys_mmap(nullptr,
                          PAGE_SIZE,
                          PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1,
                          0);
    TEST_ASSERT(addr != MAP_FAILED);
    TEST_ASSERT(sys_munmap(addr, PAGE_SIZE) == 0);
    return true;
}

TEST(test_syscall_munmap_partial)
{
    const size_t len = PAGE_SIZE * 3;
    void *addr       = sys_mmap(nullptr,
                                len,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS,
                                -1,
                                0);
    TEST_ASSERT(addr != MAP_FAILED);

    const uint64_t base = (uint64_t)addr & ~(PAGE_SIZE - 1);
    TEST_ASSERT(sys_munmap((void*)(base + PAGE_SIZE), PAGE_SIZE) == 0);
    TEST_ASSERT(sys_munmap((void*)base, PAGE_SIZE) == 0);
    TEST_ASSERT(sys_munmap((void*)(base + 2 * PAGE_SIZE), PAGE_SIZE) == 0);
    return true;
}

TEST(test_syscall_mmap_invalid_length)
{
    void *addr = sys_mmap(nullptr,
                          0,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1,
                          0);
    TEST_ASSERT(addr == MAP_FAILED);
    return true;
}

TEST(test_syscall_mmap_overlap_handling)
{
    const int fd = sys_open("/dev/fb0", O_RDWR);
    if (fd < 0)
        return false;

    constexpr size_t len = PAGE_SIZE;
    void *first          = sys_mmap((void *)0x4000000000ull,
                                    len,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED,
                                    fd,
                                    0);
    if (first == MAP_FAILED) {
        sys_close(fd);
        return false;
    }

    const uint64_t first_base = (uint64_t)first & ~(PAGE_SIZE - 1);
    void *second              = sys_mmap((void *)first_base,
                                         len,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED,
                                         fd,
                                         0);
    if (second == MAP_FAILED || second == (void *)first_base || (uint64_t)second < first_base + len) {
        sys_munmap(first, len);
        sys_close(fd);
        return false;
    }

    if (sys_munmap(first, len) != 0) {
        sys_munmap(second, len);
        sys_close(fd);
        return false;
    }

    const bool ok = (sys_munmap(second, len) == 0) && (sys_close(fd) == 0);
    return ok;
}

TEST(test_syscall_munmap_invalid)
{
    // munmap with nullptr should fail
    TEST_ASSERT(sys_munmap(nullptr, 4096) == -1);
    return true;
}

TEST(test_syscall_kill_invalid_pid)
{
    TEST_ASSERT(sys_kill(99999, 9) == -1);
    return true;
}

TEST(test_syscall_kill_protected_pids)
{
    // Should not be able to kill PID 0 or 1
    TEST_ASSERT(sys_kill(0, 9) == -1);
    TEST_ASSERT(sys_kill(1, 9) == -1);
    return true;
}

TEST(test_syscall_mknod_basic)
{
    int rc = sys_mknod("/dev/testdev", VFS_CHARDEVICE, 0);
    (void)rc;
    return true;
}

TEST(test_syscall_mknod_invalid_path)
{
    TEST_ASSERT(sys_mknod(nullptr, VFS_CHARDEVICE, 0) == -EINVAL);
    TEST_ASSERT(sys_mknod("", VFS_CHARDEVICE, 0) == -EBADPATH);
    return true;
}

TEST(test_syscall_mknod_fat32_char_device_not_supported)
{
    sys_unlink("/mnt/CHRDEV.TST");
    TEST_ASSERT(sys_mknod("/mnt/CHRDEV.TST", VFS_CHARDEVICE, 0) == -ENOTSUP);
    return true;
}

TEST(test_syscall_listen_basic)
{
    const int fd = sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    TEST_ASSERT(fd >= 0);

    sockaddr_in_t addr = {0};
    addr.sin_family    = AF_INET;
    addr.sin_port      = htons(8080);

    TEST_ASSERT(sys_bind(fd, (const sockaddr_t *)&addr, sizeof(addr)) == 0);
    TEST_ASSERT(sys_listen(fd, 8) == 0);
    TEST_ASSERT(sys_close(fd) == 0);
    return true;
}

TEST(test_syscall_socket_error_codes)
{
    TEST_ASSERT(sys_socket(99, SOCK_STREAM, IPPROTO_TCP) == -ENOTSUP);
    TEST_ASSERT(sys_socket(AF_INET, 99, 0) == -EINVAL);
    TEST_ASSERT(sys_socket(AF_INET, SOCK_STREAM, IPPROTO_UDP) == -EINVAL);

    int tcp_fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(tcp_fd >= 0);

    sockaddr_in_t tcp_addr = {0};
    tcp_addr.sin_family    = AF_INET;
    TEST_ASSERT(sys_bind(tcp_fd, (const sockaddr_t *)&tcp_addr, sizeof(tcp_addr)) == 0);
    TEST_ASSERT(sys_listen(tcp_fd, 1) == 0);
    TEST_ASSERT(sys_close(tcp_fd) == 0);

    int udp_fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    TEST_ASSERT(udp_fd >= 0);
    TEST_ASSERT(sys_listen(udp_fd, 1) == -ENOTSUP);
    TEST_ASSERT(sys_bind(-1, (const sockaddr_t *)&tcp_addr, sizeof(tcp_addr)) == -EBADF);

    uint8_t buf[8] = {0};
    sockaddr_in_t dest = {0};
    dest.sin_family    = AF_INET;
    dest.sin_port      = htons(53);
    dest.sin_addr[0]   = 8;
    dest.sin_addr[1]   = 8;
    dest.sin_addr[2]   = 8;
    dest.sin_addr[3]   = 8;

    TEST_ASSERT(sys_sendto(udp_fd, nullptr, 4, 0, (const sockaddr_t *)&dest, sizeof(dest)) == -EINVAL);
    TEST_ASSERT(sys_sendto(udp_fd, buf, sizeof(buf), 0, nullptr, 0) == -EINVAL);
    TEST_ASSERT(sys_recvfrom(udp_fd, nullptr, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr) == -EINVAL);
    TEST_ASSERT(sys_recvfrom(udp_fd, buf, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr) == -EAGAIN);

    TEST_ASSERT(sys_close(udp_fd) == 0);
    return true;
}

static bool syscall_test_build_tcp_packet(uint8_t *packet, const size_t packet_len,
                                          const uint8_t src_mac[static 6], const uint8_t dest_mac[static 6],
                                          const uint8_t src_ip[static 4], const uint8_t dest_ip[static 4],
                                          const uint16_t src_port, const uint16_t dest_port,
                                          const uint32_t seq_num, const uint32_t ack_num, const uint8_t flags,
                                          const uint8_t *payload, const size_t payload_len)
{
    constexpr size_t eth_len        = sizeof(ether_header_t);
    constexpr size_t ip_header_len  = sizeof(ipv4_header_t);
    constexpr size_t tcp_header_len = sizeof(tcp_header_t);
    const size_t ip_len             = ip_header_len + tcp_header_len + payload_len;
    if (packet_len < eth_len + ip_len)
        return false;

    memset(packet, 0, packet_len);

    auto const eth = (ether_header_t *)packet;
    memcpy(eth->dest_host, dest_mac, 6);
    memcpy(eth->src_host, src_mac, 6);
    eth->ether_type = htons(ETHERTYPE_IP);

    auto const ip             = (ipv4_header_t *)(packet + eth_len);
    ip->version               = 4;
    ip->ihl                   = (uint8_t)(ip_header_len / 4);
    ip->dscp_ecn              = 0;
    ip->total_length          = htons((uint16_t)ip_len);
    ip->identification        = 0;
    ip->flags_fragment_offset = 0;
    ip->ttl                   = 64;
    ip->protocol              = IP_PROTOCOL_TCP;
    ip->header_checksum       = 0;
    memcpy(ip->source_ip, src_ip, 4);
    memcpy(ip->dest_ip, dest_ip, 4);

    auto const tcp            = (tcp_header_t *)(packet + eth_len + ip_header_len);
    tcp->src_port             = src_port;
    tcp->dst_port             = dest_port;
    tcp->seq_num              = htonl(seq_num);
    tcp->ack_num              = htonl(ack_num);
    tcp->data_offset_reserved = (uint8_t)((tcp_header_len / 4) << 4);
    tcp->flags                = flags;
    tcp->window               = htons(4096);
    tcp->checksum             = 0;
    tcp->urgent_ptr           = 0;

    if (payload_len > 0)
        memcpy(packet + eth_len + ip_header_len + tcp_header_len, payload, payload_len);
    return true;
}

TEST(test_syscall_accept_basic)
{
    constexpr uint8_t my_ip[4]     = {10, 0, 2, 15};
    const uint8_t my_mac[6]        = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    constexpr uint8_t remote_ip[4] = {10, 0, 2, 2};
    const uint8_t remote_mac[6]    = {0x52, 0x54, 0x00, 0xAB, 0xCD, 0xEF};
    const uint16_t local_port      = htons(9090);
    const uint16_t remote_port     = htons(12345);
    constexpr uint32_t remote_seq  = 500;

    network_set_my_ip_address(my_ip);
    network_set_mac(my_mac);

    const int listen_fd = sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    TEST_ASSERT(listen_fd >= 0);

    sockaddr_in_t addr = {0};
    addr.sin_family    = AF_INET;
    addr.sin_port      = local_port;

    TEST_ASSERT(sys_bind(listen_fd, (const sockaddr_t*)&addr, sizeof(addr)) == 0);
    TEST_ASSERT(sys_listen(listen_fd, 4) == 0);

    constexpr size_t ip_header_len = sizeof(ipv4_header_t);
    constexpr size_t syn_ip_len    = ip_header_len + sizeof(tcp_header_t);
    constexpr size_t syn_len       = sizeof(ether_header_t) + syn_ip_len;
    uint8_t syn_packet[syn_len];
    TEST_ASSERT(syscall_test_build_tcp_packet(syn_packet, sizeof(syn_packet),
        remote_mac, my_mac,
        remote_ip, my_ip,
        remote_port, local_port,
        remote_seq, 0, TCP_FLAG_SYN,
        nullptr, 0));
    tcp_receive(syn_packet, (uint16_t)sizeof(syn_packet), syn_ip_len, ip_header_len);

    socket_t *pending = socket_find_tcp_connected(my_ip, local_port, remote_ip, remote_port);
    TEST_ASSERT(pending != nullptr);

    constexpr size_t ack_ip_len = ip_header_len + sizeof(tcp_header_t);
    constexpr size_t ack_len    = sizeof(ether_header_t) + ack_ip_len;
    uint8_t ack_packet[ack_len];
    TEST_ASSERT(syscall_test_build_tcp_packet(ack_packet, sizeof(ack_packet),
        remote_mac, my_mac,
        remote_ip, my_ip,
        remote_port, local_port,
        remote_seq + 1, pending->tcp_send_next, TCP_FLAG_ACK,
        nullptr, 0));
    tcp_receive(ack_packet, (uint16_t)sizeof(ack_packet), ack_ip_len, ip_header_len);
    socket_put(pending);

    sockaddr_in_t client = {0};
    socklen_t client_len = sizeof(sa_family_t) + sizeof(client.sin_port) + sizeof(client.sin_addr);
    memset(&client, 0xA5, sizeof(client));
    const int conn_fd    = sys_accept(listen_fd, (sockaddr_t *)&client, &client_len);
    TEST_ASSERT(conn_fd >= 0);
    TEST_ASSERT(conn_fd != listen_fd);
    TEST_ASSERT(client_len == sizeof(client));
    TEST_ASSERT(client.sin_family == AF_INET);
    TEST_ASSERT(client.sin_port == remote_port);
    TEST_ASSERT(memcmp(client.sin_addr, remote_ip, sizeof(client.sin_addr)) == 0);

    file_descriptor_t *conn_desc = current_process->fd_table[conn_fd];
    TEST_ASSERT(conn_desc != nullptr);
    TEST_ASSERT(conn_desc->inode != nullptr);
    socket_t *conn_sock = (socket_t *)conn_desc->inode->device;
    TEST_ASSERT(conn_sock != nullptr);
    TEST_ASSERT((conn_sock->flags & SOCKET_FLAG_TCP_ESTABLISHED) != 0);

    TEST_ASSERT(sys_close(conn_fd) == 0);
    TEST_ASSERT(sys_close(listen_fd) == 0);
    return true;
}

TEST(test_syscall_accept_nonblock_empty_queue)
{
    const int listen_fd = sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    TEST_ASSERT(listen_fd >= 0);

    sockaddr_in_t addr = {0};
    addr.sin_family    = AF_INET;
    addr.sin_port      = htons(45679);

    TEST_ASSERT(sys_bind(listen_fd, (const sockaddr_t *)&addr, sizeof(addr)) == 0);
    TEST_ASSERT(sys_listen(listen_fd, 1) == 0);
    TEST_ASSERT(sys_fcntl(listen_fd, F_SETFL, O_NONBLOCK) == 0);

    sockaddr_in_t client = {0};
    socklen_t client_len = sizeof(client);
    TEST_ASSERT(sys_accept(listen_fd, (sockaddr_t *)&client, &client_len) == -EAGAIN);

    TEST_ASSERT(sys_close(listen_fd) == 0);
    return true;
}
