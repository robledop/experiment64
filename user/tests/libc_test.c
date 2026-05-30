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

    return 0;
}
