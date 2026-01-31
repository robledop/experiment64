#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int mask_phase   = 0;
static volatile int got_usr2     = 0;
static volatile int handler_fail = 0;

static void usr2_handler([[maybe_unused]] const int sig)
{
    got_usr2 = 1;
}

__attribute__((used)) static void usr1_handler_c([[maybe_unused]] const int sig, sigcontext_t *ctx)
{
    if (!ctx) {
        handler_fail = 1;
        return;
    }

    constexpr sigset_t bit = (sigset_t)1 << (SIGUSR2 - 1);

    // On mask_phase 0, set SIGUSR2 mask bit and transition to phase 1
    // On mask_phase 1, clear SIGUSR2 mask bit and transition to phase 2
    if (mask_phase == 0) {
        ctx->sigmask |= bit;
        mask_phase   = 1;
    } else if (mask_phase == 1) {
        ctx->sigmask &= ~bit;
        mask_phase   = 2;
    }
}

__attribute__((naked)) static void usr1_handler([[maybe_unused]] const int sig)
{
    // Grab the context pointer from the stack and pass it as the second argument (rsi) to usr1_handler_c
    __asm__ volatile(
        "lea rsi, [rsp + 8]\n"
        "call usr1_handler_c\n"
        "ret\n");
}

int main(void)
{
    struct sigaction sa_usr1 = {};
    sa_usr1.sa_handler       = usr1_handler;
    if (sigaction(SIGUSR1, &sa_usr1, nullptr) < 0) {
        printf("sigtest_mask: sigaction SIGUSR1 failed\n");
        return 1;
    }

    struct sigaction sa_usr2 = {};
    sa_usr2.sa_handler       = usr2_handler;
    if (sigaction(SIGUSR2, &sa_usr2, nullptr) < 0) {
        printf("sigtest_mask: sigaction SIGUSR2 failed\n");
        return 2;
    }

    if (kill(getpid(), SIGUSR1) < 0) {
        printf("sigtest_mask: kill SIGUSR1 failed\n");
        return 3;
    }

    // The first time SIGUSR1 is delivered, SIGUSR2 is masked, and we transition to phase 1
    for (int i = 0; i < 100000; i++) {
        if (mask_phase >= 1)
            break;
        yield();
    }

    if (handler_fail) {
        printf("sigtest_mask: handler failed\n");
        return 4;
    }
    if (mask_phase < 1) {
        printf("sigtest_mask: SIGUSR1 not handled\n");
        return 5;
    }

    if (kill(getpid(), SIGUSR2) < 0) {
        printf("sigtest_mask: kill SIGUSR2 failed\n");
        return 6;
    }

    for (int i = 0; i < 100000; i++) {
        if (got_usr2)
            break;
        yield();
    }

    // We are still in phase one, SIGUSR2 is masked
    if (got_usr2) {
        printf("sigtest_mask: SIGUSR2 delivered while masked\n");
        return 7;
    }

    // The second time SIGUSR1 is delivered, SIGUSR2 is unmasked, and we transition to phase 2
    if (kill(getpid(), SIGUSR1) < 0) {
        printf("sigtest_mask: kill SIGUSR1 unmask failed\n");
        return 8;
    }

    for (int i = 0; i < 100000; i++) {
        if (mask_phase >= 2)
            break;
        yield();
    }

    if (mask_phase < 2) {
        printf("sigtest_mask: SIGUSR1 unmask handler not run\n");
        return 9;
    }

    for (int i = 0; i < 100000; i++) {
        if (got_usr2)
            break;
        yield();
    }

    // We are now in phase two, SIGUSR2 is unmasked, so got_usr2 should be true
    if (!got_usr2) {
        printf("sigtest_mask: SIGUSR2 not delivered after unmask\n");
        return 10;
    }

    // We only reach this point if SIGUSR2 was NOT delivered while masked,
    // but then WAS delivered after unmasked.
    return 11;
}