#include <tests/test.h>
#include <lib/path.h>
#include <lib/string.h>

TEST(test_path_simplify_dot)
{
    char path[PATH_MAX] = "/foo/./bar";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/foo/bar") == 0);
    return true;
}

TEST(test_path_simplify_dotdot)
{
    char path[PATH_MAX] = "/foo/bar/../baz";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/foo/baz") == 0);
    return true;
}

TEST(test_path_simplify_dotdot_at_root)
{
    char path[PATH_MAX] = "/../foo";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/foo") == 0);
    return true;
}

TEST(test_path_simplify_trailing_slash)
{
    char path[PATH_MAX] = "/foo/bar/";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/foo/bar") == 0);
    return true;
}

TEST(test_path_simplify_multiple_slashes)
{
    char path[PATH_MAX] = "/foo///bar//baz";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/foo/bar/baz") == 0);
    return true;
}

TEST(test_path_simplify_root_only)
{
    char path[PATH_MAX] = "/";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/") == 0);
    return true;
}

TEST(test_path_simplify_all_dotdot)
{
    char path[PATH_MAX] = "/a/b/../../..";
    path_simplify(path, sizeof(path));
    TEST_ASSERT(strcmp(path, "/") == 0);
    return true;
}

TEST(test_path_build_absolute_relative)
{
    char out[PATH_MAX];
    path_build_absolute("/home", "file.txt", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "/home/file.txt") == 0);
    return true;
}

TEST(test_path_build_absolute_already_absolute)
{
    char out[PATH_MAX];
    path_build_absolute("/home", "/etc/config", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "/etc/config") == 0);
    return true;
}

TEST(test_path_build_absolute_null_base)
{
    char out[PATH_MAX];
    path_build_absolute(nullptr, "file.txt", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "/file.txt") == 0);
    return true;
}

TEST(test_path_build_absolute_empty_input)
{
    char out[PATH_MAX];
    path_build_absolute("/home", "", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "/home") == 0);
    return true;
}

TEST(test_path_build_absolute_with_dotdot)
{
    char out[PATH_MAX];
    path_build_absolute("/home/user", "../other", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "/home/other") == 0);
    return true;
}

TEST(test_path_safe_copy_truncates)
{
    char dst[4];
    path_safe_copy(dst, sizeof(dst), "abcdef");
    TEST_ASSERT(strcmp(dst, "abc") == 0);
    return true;
}

TEST(test_path_safe_copy_null_src)
{
    char dst[4] = "xxx";
    path_safe_copy(dst, sizeof(dst), nullptr);
    TEST_ASSERT(dst[0] == '\0');
    return true;
}
