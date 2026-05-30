#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Userland exercise for libc correctness fixes.
 *
 * Spawned by the in-kernel test_libc_userland test, which checks that this
 * program exits 0. Each check returns a unique nonzero code on failure.
 *
 * Exit codes:
 *   0 = all passed
 *   1 = malloc did not reject an overflowing size request
 *   2 = malloc did not reject a near-overflow size request
 *   3 = normal malloc failed
 *   4 = could not allocate a setvbuf buffer
 *   5 = could not open a scratch file
 *   6 = fclose wrongly freed the caller-provided setvbuf buffer
 *   7 = could not open a scratch file for the zero-buffer check
 *   8 = a fully-buffered write with a zero-size buffer did not complete
 */
int main(void)
{
    // malloc must reject a request so large that the unit computation would
    // overflow, instead of wrapping around and returning an undersized buffer.
    if (malloc((size_t)-1) != NULL)
        return 1;
    if (malloc((size_t)-8) != NULL)
        return 2;

    // A normal allocation must still succeed.
    void *p = malloc(64);
    if (!p)
        return 3;
    free(p);

    // fclose must not free a caller-provided setvbuf buffer. Detect a wrongful
    // free by checking the allocator does not immediately hand the buffer back.
    char *ubuf = malloc(BUFSIZ);
    if (!ubuf)
        return 4;
    FILE *f = fopen("/libc_test.tmp", "w");
    if (!f) {
        free(ubuf);
        return 5;
    }
    setvbuf(f, ubuf, _IOFBF, BUFSIZ);
    fwrite("hello", 1, 5, f);
    fclose(f);

    char *again = malloc(BUFSIZ);
    if (again == ubuf)
        return 6;
    free(again);
    free(ubuf);

    // A fully-buffered stream given a zero-size buffer must not spin forever on
    // a write; the write should complete (falling back to unbuffered).
    char zero_buf[1];
    FILE *zf = fopen("/libc_test.tmp", "w");
    if (!zf)
        return 7;
    setvbuf(zf, zero_buf, _IOFBF, 0);
    size_t n = fwrite("x", 1, 1, zf);
    fclose(zf);
    if (n != 1)
        return 8;

    return 0;
}
