#include <tests/test.h>
#include <sys/syscall.h>
#include <sys/poll.h>
#include <lib/string.h>

TEST(test_pipe_read_eof_on_closed_writer)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    // Write some data then close write end.
    TEST_ASSERT(sys_write(pipefd[1], "abc", 3) == 3);
    TEST_ASSERT(sys_close(pipefd[1]) == 0);

    // Read should return the buffered data.
    char buf[8] = {0};
    TEST_ASSERT(sys_read(pipefd[0], buf, sizeof(buf)) == 3);
    TEST_ASSERT(strncmp(buf, "abc", 3) == 0);

    // Next read should return 0 (EOF).
    TEST_ASSERT(sys_read(pipefd[0], buf, sizeof(buf)) == 0);

    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    return true;
}

TEST(test_pipe_write_to_closed_reader)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    // Close the read end.
    TEST_ASSERT(sys_close(pipefd[0]) == 0);

    // Writing to a pipe with no readers should return 0 (broken pipe).
    int written = sys_write(pipefd[1], "data", 4);
    TEST_ASSERT(written == 0);

    TEST_ASSERT(sys_close(pipefd[1]) == 0);
    return true;
}

TEST(test_pipe_poll_writer_closed)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    TEST_ASSERT(sys_close(pipefd[1]) == 0);

    struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
    TEST_ASSERT(sys_poll(&pfd, 1, 0) == 1);
    TEST_ASSERT((pfd.revents & POLLHUP) != 0);

    TEST_ASSERT(sys_close(pipefd[0]) == 0);
    return true;
}

TEST(test_pipe_poll_reader_closed)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    TEST_ASSERT(sys_close(pipefd[0]) == 0);

    struct pollfd pfd = {.fd = pipefd[1], .events = POLLOUT, .revents = 0};
    TEST_ASSERT(sys_poll(&pfd, 1, 0) == 1);
    TEST_ASSERT((pfd.revents & POLLERR) != 0);

    TEST_ASSERT(sys_close(pipefd[1]) == 0);
    return true;
}

TEST(test_pipe_large_write_read_cycle)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    // Fill the pipe completely (4096 bytes).
    char fill[256];
    memset(fill, 'X', sizeof(fill));
    int total = 0;
    for (int i = 0; i < 16; i++)
    {
        int w = sys_write(pipefd[1], fill, sizeof(fill));
        if (w > 0) total += w;
    }
    TEST_ASSERT(total == 4096);

    // Drain it all.
    char drain[4096];
    int r = sys_read(pipefd[0], drain, sizeof(drain));
    TEST_ASSERT(r == 4096);

    // Pipe should be empty now — write should succeed again.
    TEST_ASSERT(sys_write(pipefd[1], "ok", 2) == 2);
    char buf[4] = {0};
    TEST_ASSERT(sys_read(pipefd[0], buf, sizeof(buf)) == 2);
    TEST_ASSERT(strncmp(buf, "ok", 2) == 0);

    sys_close(pipefd[0]);
    sys_close(pipefd[1]);
    return true;
}
