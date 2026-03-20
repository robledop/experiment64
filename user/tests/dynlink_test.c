#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * @brief End-to-end test for dynamic linking.
 *
 * Exercises the full dynamic linking chain: ld.so bootstrap, library loading,
 * symbol resolution, and relocation. Each test returns a unique nonzero exit
 * code on failure for easy diagnosis.
 *
 * Exit codes:
 *   0  = all passed
 *   1  = printf (JUMP_SLOT relocation) failed
 *   2  = malloc/free (heap via libc.so) failed
 *   3  = strcmp (JUMP_SLOT to libc function) failed
 *   4  = strlen failed
 *   5  = memset failed
 *   6  = snprintf failed
 *   7  = errno (TLS across dynamic boundary) failed
 *   8  = atoi failed
 *   9  = multiple libc calls in sequence failed
 */
int main(void)
{
    /* Test 1: printf — proves JUMP_SLOT relocation for a PLT call works */
    int n = printf("dynlink_test: starting\r\n");
    if (n <= 0)
        return 1;

    /* Test 2: malloc/free — proves heap works from dynamically linked libc */
    char *buf = malloc(128);
    if (!buf)
        return 2;
    memset(buf, 'A', 127);
    buf[127] = '\0';
    if (strlen(buf) != 127)
    {
        free(buf);
        return 5;
    }
    free(buf);

    /* Test 3: strcmp — basic string comparison from libc.so */
    if (strcmp("hello", "hello") != 0)
        return 3;
    if (strcmp("aaa", "bbb") >= 0)
        return 3;

    /* Test 4: strlen on a stack string */
    const char *test_str = "dynamic";
    if (strlen(test_str) != 7)
        return 4;

    /* Test 5: memset — bulk memory operation */
    char arr[64];
    memset(arr, 0x42, sizeof(arr));
    for (int i = 0; i < 64; i++)
    {
        if (arr[i] != 0x42)
            return 5;
    }

    /* Test 6: snprintf — formatted string with buffer limit */
    char fmt_buf[32];
    int written = snprintf(fmt_buf, sizeof(fmt_buf), "val=%d", 42);
    if (written != 6)
        return 6;
    if (strcmp(fmt_buf, "val=42") != 0)
        return 6;

    /* Test 7: errno — thread-local storage across dynamic boundary */
    errno = 0;
    if (errno != 0)
        return 7;
    errno = 22;
    if (errno != 22)
        return 7;
    errno = 0;

    /* Test 8: atoi — string-to-int conversion */
    if (atoi("12345") != 12345)
        return 8;
    if (atoi("-1") != -1)
        return 8;

    /* Test 9: multiple calls in sequence to stress relocation */
    for (int i = 0; i < 10; i++)
    {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%d", i);
        if (atoi(tmp) != i)
            return 9;
    }

    printf("dynlink_test: all passed\r\n");
    return 0;
}
