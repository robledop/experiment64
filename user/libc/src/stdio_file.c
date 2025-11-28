#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

// Simple FILE abstraction backed by an in-memory buffer loaded from disk.

static int parse_mode(const char *mode, bool *out_write, bool *out_append)
{
    bool w = false;
    bool a = false;
    for (const char *p = mode; *p; p++)
    {
        if (*p == 'w')
            w = true;
        else if (*p == 'a')
            a = true;
    }
    *out_write = w || a;
    *out_append = a;
    return 0;
}

FILE __stdin_file_obj = {.fd = 0, .data = nullptr, .size = 0, .pos = 0, .mode = 0, .is_mem = false, .dirty = false};
FILE __stdout_file_obj = {.fd = 1, .data = nullptr, .size = 0, .pos = 0, .mode = 0, .is_mem = false, .dirty = false};
FILE __stderr_file_obj = {.fd = 2, .data = nullptr, .size = 0, .pos = 0, .mode = 0, .is_mem = false, .dirty = false};
FILE *__stdin_file = &__stdin_file_obj;
FILE *__stdout_file = &__stdout_file_obj;
FILE *__stderr_file = &__stderr_file_obj;

static int load_file_into_memory(FILE *f, const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return -1;

    f->size = (size_t)st.size;
    f->data = malloc(f->size);
    if (!f->data)
        return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    size_t read_total = 0;
    while (read_total < f->size)
    {
        ssize_t r = read(fd, f->data + read_total, f->size - read_total);
        if (r <= 0)
            break;
        read_total += (size_t)r;
    }
    close(fd);
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode)
        return nullptr;
    FILE *f = malloc(sizeof(FILE));
    if (!f)
        return nullptr;
    memset(f, 0, sizeof(FILE));
    f->fd = -1;
    bool write_mode = false, append_mode = false;
    parse_mode(mode, &write_mode, &append_mode);
    f->mode = write_mode ? 1 : 0;
    strncpy(f->path, path, sizeof(f->path) - 1);
    f->path[sizeof(f->path) - 1] = '\0';

    if (!write_mode)
    {
        if (load_file_into_memory(f, path) != 0)
        {
            free(f);
            return nullptr;
        }
    }
    else
    {
        f->data = nullptr;
        f->size = 0;
        f->dirty = true;
        f->fd = open(path, O_CREATE | O_TRUNC | O_RDWR);
        if (f->fd < 0)
        {
            free(f);
            return nullptr;
        }
    }
    if (append_mode)
        f->pos = f->size;
    return f;
}

int fclose(FILE *stream)
{
    if (!stream)
        return -1;
    if (stream == __stdin_file || stream == __stdout_file || stream == __stderr_file)
        return 0;

    if (stream->dirty)
    {
        int fd = open(stream->path, O_CREATE | O_TRUNC | O_RDWR);
        if (fd >= 0)
        {
            write(fd, stream->data, stream->size);
            close(fd);
        }
    }
    free(stream->data);
    free(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0)
        return 0;
    size_t bytes = size * nmemb;
    if (stream->pos >= stream->size)
        return 0;
    if (stream->pos + bytes > stream->size)
        bytes = stream->size - stream->pos;
    memcpy(ptr, stream->data + stream->pos, bytes);
    stream->pos += bytes;
    return bytes / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0)
        return 0;
    size_t bytes = size * nmemb;
    size_t needed = stream->pos + bytes;
    if (needed > stream->size)
    {
        char *newbuf = realloc(stream->data, needed);
        if (!newbuf)
            return 0;
        stream->data = newbuf;
        stream->size = needed;
    }
    memcpy(stream->data + stream->pos, ptr, bytes);
    stream->pos += bytes;
    stream->dirty = true;
    return nmemb;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream)
        return -1;
    size_t newpos = 0;
    switch (whence)
    {
    case SEEK_SET:
        newpos = (offset < 0) ? 0 : (size_t)offset;
        break;
    case SEEK_CUR:
        if (offset < 0 && (size_t)(-offset) > stream->pos)
            newpos = 0;
        else
            newpos = stream->pos + offset;
        break;
    case SEEK_END:
        if (offset < 0 && (size_t)(-offset) > stream->size)
            newpos = 0;
        else
            newpos = stream->size + offset;
        break;
    default:
        return -1;
    }
    stream->pos = newpos;
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream)
        return -1;
    return (long)stream->pos;
}

int fflush(FILE *stream)
{
    if (!stream)
        return -1;
    // Nothing to do; writes are buffered in memory and flushed on close.
    return 0;
}

int vfprintf(FILE *stream, const char *format, va_list args)
{
    char buf[1024];
    int len = vsnprintf(buf, sizeof buf, format, args);
    fwrite(buf, 1, (size_t)len, stream);
    if (stream == __stdout_file || stream == __stderr_file)
        write(stream->fd, buf, (size_t)len);
    return len;
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int res = vfprintf(stream, format, args);
    va_end(args);
    return res;
}

int remove(const char *path)
{
    return unlink(path);
}

int rename(const char *oldpath, const char *newpath)
{
    return link(oldpath, newpath) || unlink(oldpath);
}
