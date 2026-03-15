#include <tests/test.h>
#include <lib/string.h>
#include <limits.h>

TEST(test_strlen)
{
    TEST_ASSERT(strlen("") == 0);
    TEST_ASSERT(strlen("hello") == 5);
    return true;
}

TEST(test_strcmp)
{
    TEST_ASSERT(strcmp("a", "a") == 0);
    TEST_ASSERT(strcmp("a", "b") < 0);
    TEST_ASSERT(strcmp("b", "a") > 0);
    return true;
}

TEST(test_memcpy)
{
    constexpr char src[] = "hello";
    char dest[6];
    memcpy(dest, src, 6);
    TEST_ASSERT(strcmp(dest, "hello") == 0);
    return true;
}

TEST(test_memset)
{
    char buf[5];
    memset(buf, 'a', 5);
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT(buf[i] == 'a');
    }
    return true;
}

TEST(test_strncmp)
{
    TEST_ASSERT(strncmp("abc", "abd", 2) == 0);
    TEST_ASSERT(strncmp("abc", "abd", 3) < 0);
    TEST_ASSERT(strncmp("abc", "abc", 5) == 0);
    return true;
}

TEST(test_memcmp)
{
    constexpr char a[] = {1, 2, 3};
    constexpr char b[] = {1, 2, 4};
    TEST_ASSERT(memcmp(a, b, 2) == 0);
    TEST_ASSERT(memcmp(a, b, 3) < 0);
    return true;
}

TEST(test_memcmp_zero_length)
{
    TEST_ASSERT(memcmp("abc", "xyz", 0) == 0);
    return true;
}

TEST(test_strncmp_zero_length)
{
    TEST_ASSERT(strncmp("abc", "xyz", 0) == 0);
    return true;
}

TEST(test_strcat_and_strrchr)
{
    char buf[16] = "hi";
    strcat(buf, " there");
    TEST_ASSERT(strcmp(buf, "hi there") == 0);

    const char* p = strrchr(buf, 'h');
    TEST_ASSERT(p != nullptr);
    TEST_ASSERT(strcmp(p, "here") == 0); // last 'h' in "there"
    return true;
}

TEST(test_strncpy_pads_with_nulls)
{
    char buf[5];
    strncpy(buf, "abc", sizeof(buf));
    TEST_ASSERT(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c');
    TEST_ASSERT(buf[3] == '\0' && buf[4] == '\0');
    return true;
}

// Formatting tests for snprintk/vsnprintk
TEST(test_snprintk_basic_formats)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "%d %u %x %p", -123, 456u, 0x1au, (void*)0);
    TEST_ASSERT(written == (int)strlen("-123 456 1a 0x0"));
    TEST_ASSERT(strcmp(buf, "-123 456 1a 0x0") == 0);
    return true;
}

TEST(test_snprintk_truncation_counts)
{
    char buf[5];
    int written = snprintk(buf, sizeof(buf), "%s", "abcdef");
    TEST_ASSERT(written == 6); // full length
    TEST_ASSERT(strcmp(buf, "abcd") == 0); // truncated output

    char buf2[1];
    written = snprintk(buf2, sizeof(buf2), "%s", "hi");
    TEST_ASSERT(written == 2);
    TEST_ASSERT(buf2[0] == '\0');
    return true;
}

TEST(test_snprintk_zero_size_buffer)
{
    char buf[4] = {'X', 'X', 'X', 'X'};
    int written = snprintk(buf, 0, "abc");
    TEST_ASSERT(written == 3);
    TEST_ASSERT(buf[0] == 'X'); // untouched because size == 0
    return true;
}

TEST(test_snprintk_pointer_and_hex_uppercase)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "%p %X", (void*)0x1234, 0xBEEF);
    TEST_ASSERT(written == (int)strlen("0x1234 BEEF"));
    TEST_ASSERT(strcmp(buf, "0x1234 BEEF") == 0);
    return true;
}

TEST(test_snprintk_llong_min)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "%lld", (long long)LLONG_MIN);
    TEST_ASSERT(written == (int)strlen("-9223372036854775808"));
    TEST_ASSERT(strcmp(buf, "-9223372036854775808") == 0);
    return true;
}

TEST(test_snprintk_left_align_flag)
{
    char buf[32];
    int written = snprintk(buf, sizeof(buf), "%-10s", "hi");
    TEST_ASSERT(written == (int)strlen("hi        "));
    TEST_ASSERT(strcmp(buf, "hi        ") == 0);

    written = snprintk(buf, sizeof(buf), "%10s", "hi");
    TEST_ASSERT(written == (int)strlen("        hi"));
    TEST_ASSERT(strcmp(buf, "        hi") == 0);
    return true;
}

TEST(test_snprintk_zero_pad_flag)
{
    char buf[64];
    int written = snprintk(buf, sizeof(buf), "0x%08x %08u %08d %08o", 0x1f, 23u, -23, 9u);
    TEST_ASSERT(written == (int)strlen("0x0000001f 00000023 -0000023 00000011"));
    TEST_ASSERT(strcmp(buf, "0x0000001f 00000023 -0000023 00000011") == 0);
    return true;
}

TEST(test_memmove_no_overlap)
{
    char src[] = "hello";
    char dst[6];
    memmove(dst, src, 6);
    TEST_ASSERT(strcmp(dst, "hello") == 0);
    return true;
}

TEST(test_memmove_overlap_forward)
{
    // src overlaps dst, src < dst
    char buf[10] = "abcdefgh";
    memmove(buf + 2, buf, 6); // "ababcdef"
    TEST_ASSERT(buf[0] == 'a' && buf[1] == 'b');
    TEST_ASSERT(buf[2] == 'a' && buf[3] == 'b' && buf[4] == 'c');
    TEST_ASSERT(buf[5] == 'd' && buf[6] == 'e' && buf[7] == 'f');
    return true;
}

TEST(test_memmove_overlap_backward)
{
    // dst overlaps src, dst < src
    char buf[10] = "abcdefgh";
    memmove(buf, buf + 2, 6); // "cdefef.."
    TEST_ASSERT(buf[0] == 'c' && buf[1] == 'd' && buf[2] == 'e');
    TEST_ASSERT(buf[3] == 'f' && buf[4] == 'g' && buf[5] == 'h');
    return true;
}

TEST(test_memmove_zero_length)
{
    char buf[4] = "abc";
    memmove(buf, buf + 1, 0);
    TEST_ASSERT(buf[0] == 'a'); // unchanged
    return true;
}
