#include <path.h>
#include <string.h>

void path_safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    while (i + 1 < dst_size && src[i])
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void path_simplify(char *path, size_t path_size)
{
    if (!path || path_size == 0)
        return;

    char buffer[PATH_MAX_LEN];
    path_safe_copy(buffer, sizeof(buffer), path);

    char *segments[PATH_MAX_SEGMENTS];
    int seg_count = 0;
    char *p = buffer;

    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        char *segment = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = '\0';

        if (strcmp(segment, ".") == 0)
            continue;
        if (strcmp(segment, "..") == 0)
        {
            if (seg_count > 0)
                seg_count--;
            continue;
        }
        if (seg_count < PATH_MAX_SEGMENTS)
            segments[seg_count++] = segment;
    }

    size_t idx = 0;
    path[idx++] = '/';
    for (int i = 0; i < seg_count && idx < path_size - 1; i++)
    {
        const char *seg = segments[i];
        for (size_t j = 0; seg[j] && idx < path_size - 1; j++)
            path[idx++] = seg[j];
        if (i != seg_count - 1 && idx < path_size - 1)
            path[idx++] = '/';
    }

    if (idx == 1)
        path[1] = '\0';
    else
        path[idx] = '\0';
}

void path_build_absolute(const char *base, const char *input, char *output, size_t size)
{
    if (!output || size == 0)
        return;

    const char *root = (base && base[0]) ? base : "/";

    if (!input || !*input)
    {
        path_safe_copy(output, size, root);
        return;
    }

    if (*input == '/')
    {
        path_safe_copy(output, size, input);
    }
    else
    {
        path_safe_copy(output, size, root);
        size_t idx = strlen(output);
        if (idx == 0)
        {
            output[0] = '/';
            output[1] = '\0';
            idx = 1;
        }
        if (idx > 1 && output[idx - 1] != '/' && idx + 1 < size)
            output[idx++] = '/';

        for (size_t i = 0; input[i] && idx + 1 < size; i++)
            output[idx++] = input[i];
        output[idx] = '\0';
    }

    path_simplify(output, size);
}
