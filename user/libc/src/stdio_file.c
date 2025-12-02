#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static void parse_mode(const char *mode, bool *out_read, bool *out_write, bool *out_append, bool *out_trunc, bool *out_create)
{
    bool r = false, w = false, a = false, plus = false;
    for (const char *p = mode; *p; p++)
    {
        if (*p == 'r') r = true;
        if (*p == 'w') w = true;
        if (*p == 'a') a = true;
        if (*p == '+') plus = true;
    }
    bool readable = r || (!w && !a) || plus;
    bool writable = w || a || plus;
    *out_read = readable;
    *out_write = writable;
    *out_append = a;
    if (out_trunc)
        *out_trunc = w;
    if (out_create)
        *out_create = w || a;
}

FILE __stdin_file_obj = {.fd = 0, .readable = true, .writable = false, .append = false, .need_seek = false, .size = 0, .pos = 0, .open_flags = O_RDONLY, .path = ""};
FILE __stdout_file_obj = {.fd = 1, .readable = false, .writable = true, .append = false, .need_seek = false, .size = 0, .pos = 0, .open_flags = O_WRONLY, .path = ""};
FILE __stderr_file_obj = {.fd = 2, .readable = false, .writable = true, .append = false, .need_seek = false, .size = 0, .pos = 0, .open_flags = O_WRONLY, .path = ""};
FILE *__stdin_file = &__stdin_file_obj;
FILE *__stdout_file = &__stdout_file_obj;
FILE *__stderr_file = &__stderr_file_obj;

static int seek_to_position(FILE *f, size_t target, bool for_write)
{
    if (!f)
        return -1;
    if (f->path[0] == '\0')
        return f->fd >= 0 ? 0 : -1;

    // Reopen file if fd is closed
    if (f->fd < 0)
    {
        int flags = f->open_flags;
        if (for_write && f->append)
            flags |= O_APPEND;

        int fd = open(f->path, flags);
        if (fd < 0)
        {
            f->fd = -1;
            return -1;
        }
        f->fd = fd;
    }

    // Use lseek for positioning
    size_t seek_target = (for_write && f->append) ? f->size : target;
    long result = lseek(f->fd, (long)seek_target, SEEK_SET);
    if (result < 0)
        return -1;

    f->pos = seek_target;
    return 0;
}

static int ensure_position(FILE *f, size_t target, bool for_write)
{
    if (!f)
        return -1;

    if (f->data)
    {
        f->pos = target;
        f->need_seek = false;
        return 0;
    }

    if (f->path[0] == '\0')
    {
        f->pos = target;
        f->need_seek = false;
        return 0;
    }

    if (!f->need_seek && f->fd >= 0 && f->pos == target)
        return 0;

    int res = seek_to_position(f, target, for_write);
    if (res == 0)
    {
        f->pos = target;
        f->need_seek = false;
    }
    return res;
}

FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode)
        return nullptr;

    bool rd = false, wr = false, ap = false, trunc = false, create = false;
    parse_mode(mode, &rd, &wr, &ap, &trunc, &create);

    FILE *f = malloc(sizeof(FILE));
    if (!f)
        return nullptr;
    memset(f, 0, sizeof(FILE));
    strncpy(f->path, path, sizeof(f->path) - 1);

    f->readable = rd || (!wr && !ap);
    f->writable = wr;
    f->append = ap;
    f->need_seek = false;
    if (f->readable && f->writable)
        f->open_flags = O_RDWR;
    else if (f->writable)
        f->open_flags = O_WRONLY;
    else
        f->open_flags = O_RDONLY;
    if (create)
        f->open_flags |= O_CREATE;
    if (trunc && !ap)
        f->open_flags |= O_TRUNC;
    f->pos = 0;
    f->fd = open(path, f->open_flags);
    f->data = nullptr;

    if (f->fd < 0)
    {
        free(f);
        return nullptr;
    }
    if (f->open_flags & O_TRUNC)
        f->open_flags &= ~O_TRUNC;

    struct stat st;
    if (fstat(f->fd, &st) == 0)
        f->size = (size_t)st.size;
    else
        f->size = 0;

    // We use lseek() for seeking, so no need to buffer the entire file

    if (ap)
    {
        f->pos = f->size;
        if (f->pos > 0)
            f->need_seek = true;
    }
    else
    {
        f->pos = 0;
    }
    return f;
}

int fclose(FILE *stream)
{
    if (!stream)
        return -1;
    if (stream == __stdin_file || stream == __stdout_file || stream == __stderr_file)
        return 0;
    if (stream->fd >= 0)
        close(stream->fd);
    if (stream->data)
        free(stream->data);
    free(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0 || !stream->readable)
        return 0;
    size_t bytes = size * nmemb;
    if (bytes == 0)
        return 0;

    if (stream->data)
    {
        if (stream->pos >= stream->size)
            return 0;
        if (bytes > stream->size - stream->pos)
            bytes = stream->size - stream->pos;
        memcpy(ptr, stream->data + stream->pos, bytes);
        stream->pos += bytes;
        return bytes / size;
    }

    if (stream->path[0] == '\0' && stream->fd >= 0)
    {
        ssize_t direct = read(stream->fd, ptr, bytes);
        if (direct <= 0)
            return 0;
        stream->pos += (size_t)direct;
        return (size_t)direct / size;
    }

    if (ensure_position(stream, stream->pos, false) != 0)
        return 0;

    ssize_t r = read(stream->fd, ptr, bytes);
    if (r <= 0)
        return 0;
    stream->pos += (size_t)r;
    return (size_t)r / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || !ptr || size == 0 || !stream->writable)
        return 0;
    size_t bytes = size * nmemb;
    if (bytes == 0)
        return 0;

    if (stream->path[0] == '\0' && stream->fd >= 0)
    {
        ssize_t direct = write(stream->fd, ptr, bytes);
        if (direct <= 0)
            return 0;
        stream->pos += (size_t)direct;
        if (stream->pos > stream->size)
            stream->size = stream->pos;
        return (size_t)direct / size;
    }

    size_t target = stream->append ? stream->size : stream->pos;
    if (ensure_position(stream, target, true) != 0)
        return 0;

    ssize_t w = write(stream->fd, ptr, bytes);
    if (w <= 0)
        return 0;
    stream->pos = target + (size_t)w;
    if (stream->pos > stream->size)
        stream->size = stream->pos;
    return (size_t)w / size;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream)
        return -1;
    size_t newpos = stream->pos;
    switch (whence)
    {
    case SEEK_SET:
        newpos = (offset < 0) ? 0 : (size_t)offset;
        break;
    case SEEK_CUR:
        newpos = (offset < 0 && (size_t)(-offset) > stream->pos) ? 0 : stream->pos + offset;
        break;
    case SEEK_END:
        newpos = (offset < 0 && (size_t)(-offset) > stream->size) ? 0 : stream->size + offset;
        break;
    default:
        return -1;
    }
    stream->pos = newpos;
    stream->need_seek = (stream->data == nullptr && stream->path[0] != '\0');
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream)
        return -1;
    return (long)stream->pos;
}

int fflush([[maybe_unused]] FILE *stream)
{
    return 0;
}

int vfprintf(FILE *stream, const char *format, va_list args)
{
    char buf[1024];
    int len = vsnprintf(buf, sizeof buf, format, args);
    if (len < 0)
        return len;
    size_t to_write = (len >= (int)sizeof buf) ? (sizeof buf - 1) : (size_t)len;
    fwrite(buf, 1, to_write, stream);
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
