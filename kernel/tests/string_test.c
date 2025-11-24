#include "test.h"
#include "string.h"
#include <limits.h>

TEST(test_strlen)
{
    ASSERT(strlen("") == 0);
    ASSERT(strlen("hello") == 5);
    return true;
}

TEST(test_strcmp)
{
    ASSERT(strcmp("a", "a") == 0);
    ASSERT(strcmp("a", "b") < 0);
    ASSERT(strcmp("b", "a") > 0);
    return true;
}

TEST(test_memcpy)
{
    char src[] = "hello";
    char dest[6];
    memcpy(dest, src, 6);
    ASSERT(strcmp(dest, "hello") == 0);
    return true;
}

TEST(test_memset)
{
    char buf[5];
    memset(buf, 'a', 5);
    for (int i = 0; i < 5; i++)
    {
        ASSERT(buf[i] == 'a');
    }
    return true;
}

TEST(test_strncmp)
{
    ASSERT(strncmp("abc", "abd", 2) == 0);
    ASSERT(strncmp("abc", "abd", 3) < 0);
    ASSERT(strncmp("abc", "abc", 5) == 0);
    return true;
}

TEST(test_memcmp)
{
    char a[] = {1, 2, 3};
    char b[] = {1, 2, 4};
    ASSERT(memcmp(a, b, 2) == 0);
    ASSERT(memcmp(a, b, 3) < 0);
    return true;
}

TEST(test_memcmp_zero_length)
{
    ASSERT(memcmp("abc", "xyz", 0) == 0);
    return true;
}

TEST(test_strncmp_zero_length)
{
    ASSERT(strncmp("abc", "xyz", 0) == 0);
    return true;
}

TEST(test_strcat_and_strrchr)
{
    char buf[16] = "hi";
    strcat(buf, " there");
    ASSERT(strcmp(buf, "hi there") == 0);

    const char *p = strrchr(buf, 'h');
    ASSERT(p != NULL);
    ASSERT(strcmp(p, "here") == 0); // last 'h' in "there"
    return true;
}

TEST(test_strncpy_pads_with_nulls)
{
    char buf[5];
    strncpy(buf, "abc", sizeof(buf));
    ASSERT(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c');
    ASSERT(buf[3] == '\0' && buf[4] == '\0');
    return true;
}

// Formatting tests for snprintk/vsnprintk
TEST(test_snprintk_basic_formats)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "%d %u %x %p", -123, 456u, 0x1au, (void *)0);
    ASSERT(written == (int)strlen("-123 456 1a 0x0"));
    ASSERT(strcmp(buf, "-123 456 1a 0x0") == 0);
    return true;
}

TEST(test_snprintk_truncation_counts)
{
    char buf[5];
    int written = snprintk(buf, sizeof(buf), "%s", "abcdef");
    ASSERT(written == 6);          // full length
    ASSERT(strcmp(buf, "abcd") == 0); // truncated output

    char buf2[1];
    written = snprintk(buf2, sizeof(buf2), "%s", "hi");
    ASSERT(written == 2);
    ASSERT(buf2[0] == '\0');
    return true;
}

TEST(test_snprintk_zero_size_buffer)
{
    char buf[4] = {'X', 'X', 'X', 'X'};
    int written = snprintk(buf, 0, "abc");
    ASSERT(written == 3);
    ASSERT(buf[0] == 'X'); // untouched because size == 0
    return true;
}

TEST(test_snprintk_pointer_and_hex_uppercase)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "%p %X", (void *)0x1234, 0xBEEF);
    ASSERT(written == (int)strlen("0x1234 BEEF"));
    ASSERT(strcmp(buf, "0x1234 BEEF") == 0);
    return true;
}

TEST(test_snprintk_llong_min)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "%lld", (long long)LLONG_MIN);
    ASSERT(written == (int)strlen("-9223372036854775808"));
    ASSERT(strcmp(buf, "-9223372036854775808") == 0);
    return true;
}
