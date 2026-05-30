#include <tests/test.h>
#include <sys/syscall.h>

/**
 * @brief End-to-end: run the userland libc test binary and verify exit 0.
 *
 * /tests/libc_test exercises libc correctness fixes (malloc overflow handling,
 * stdio buffering) and returns a nonzero exit code on the first failing check.
 */
TEST(test_libc_userland)
{
    int pid = sys_spawn("/tests/libc_test");
    TEST_ASSERT(pid > 1);

    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}
