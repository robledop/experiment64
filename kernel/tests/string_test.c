#include "test.h"
#include "string.h"

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
