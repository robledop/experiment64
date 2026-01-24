#include <unistd.h>

[[noreturn]] __attribute__((naked)) static void stack_align_worker([[maybe_unused]] void* arg)
{
    __asm__ volatile(
        "mov rax, rsp\n"
        "and rax, 0xf\n"
        "cmp rax, 8\n"
        "sete al\n"
        "movzx edi, al\n"
        "xor edi, 1\n"
        "mov eax, %c0\n"
        "syscall\n"
        "hlt\n"
        :
        : "i"(SYS_THREAD_EXIT)
        : "rax", "rdi", "rcx", "r11", "memory");
}

int main(void)
{
    int tid = thread_create(stack_align_worker, nullptr);
    if (tid < 0)
        return 1;
    int status = 0;
    if (thread_join(tid, &status) != 0)
        return 2;
    if (status != 0)
        return 3;
    return 0;
}
