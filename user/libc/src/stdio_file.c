/**
 * @file stdio_file.c
 * @brief Buffered FILE streams: stream objects, the three standard streams,
 *        and all write buffering.
 *
 * The FILE type (struct stdio_file) is declared in <stdio.h>; this file defines
 * the storage for the standard streams (__stdin/out/err_file_obj plus the
 * stdout alias) and the open/close/read/write/seek operations over them. The
 * write path is the interesting part:
 *
 *   - __buffered_write() copies into the per-stream wbuf honoring buf_mode
 *     (_IONBF direct, _IOLBF flush-on-newline, _IOFBF flush-when-full)
 *   - __flush_wbuf() drains the buffer to the fd; __init_wbuf()/setvbuf() set
 *     buffering policy (line-buffered iff isatty)
 *
 * Formatting is NOT here: vfprintf() renders into a stack buffer via
 * vsnprintf() (the engine in stdio.c) and then hands the bytes to fwrite(), so
 * the whole printf family ultimately bottoms out in __buffered_write() here.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void parse_mode(const char *mode, bool *out_read, bool *out_write, bool *out_append, bool *out_trunc,
                       bool *out_create)
{
    bool r = false, w = false, a = false, plus = false;
    for (const char *p = mode; *p; p++) {
        if (*p == 'r')
            r = true;
        if (*p == 'w')
            w = true;
        if (*p == 'a')
            a = true;
        if (*p == '+')
            plus = true;
    }
    bool readable = r || (!w && !a) || plus;
    bool writable = w || a || plus;
    *out_read     = readable;
    *out_write    = writable;
    *out_append   = a;
    if (out_trunc)
        *out_trunc = w;
    if (out_create)
        *out_create = w || a;
}

struct stdio_file __stdin_file_obj = {
    .fd         = 0,
    .readable   = true,
    .writable   = false,
    .append     = false,
    .need_seek  = false,
    .size       = 0,
    .pos        = 0,
    .open_flags = O_RDONLY,
    .path       = "",
    .wbuf       = nullptr,
    .wbuf_size  = 0,
    .wbuf_pos   = 0,
    .buf_mode   = _IONBF,
};
struct stdio_file __stdout_file_obj = {
    .fd         = 1,
    .readable   = false,
    .writable   = true,
    .append     = false,
    .need_seek  = false,
    .size       = 0,
    .pos        = 0,
    .open_flags = O_WRONLY,
    .path       = "",
    .wbuf       = __stdout_file_obj.wbuf_static,
    .wbuf_size  = BUFSIZ,
    .wbuf_pos   = 0,
    .buf_mode   = _IOLBF,
};
struct stdio_file __stderr_file_obj = {
    .fd         = 2,
    .readable   = false,
    .writable   = true,
    .append     = false,
    .need_seek  = false,
    .size       = 0,
    .pos        = 0,
    .open_flags = O_WRONLY,
    .path       = "",
    .wbuf       = nullptr,
    .wbuf_size  = 0,
    .wbuf_pos   = 0,
    .buf_mode   = _IONBF,
};
FILE *__stdin_file  = &__stdin_file_obj;
FILE *__stdout_file = &__stdout_file_obj;
FILE *__stderr_file = &__stderr_file_obj;

#undef stdout
FILE *const stdout = &__stdout_file_obj;

static bool stdio_is_standard_stream(const FILE *stream)
{
    return stream == __stdin_file || stream == __stdout_file || stream == __stderr_file;
}

/// @brief Flush the write buffer of a FILE stream to its fd.
/// @return 0 on success, EOF on error.
static int __flush_wbuf(FILE *f)
{
    if (!f || !f->wbuf || f->wbuf_pos == 0)
        return 0;

    const char *p    = f->wbuf;
    size_t remaining = f->wbuf_pos;
    while (remaining > 0) {
        ssize_t w = write(f->fd, p, remaining);
        if (w <= 0)
            return EOF;
        p += w;
        remaining -= (size_t)w;
    }
    f->wbuf_pos = 0;
    return 0;
}

static int stdio_flush_standard_outputs(void)
{
    int result = 0;
    if (__flush_wbuf(__stdout_file) != 0)
        result = EOF;
    if (__flush_wbuf(__stderr_file) != 0)
        result = EOF;
    return result;
}

/// @brief Write bytes through the FILE's write buffer.
/// @return Number of bytes consumed from src.
static size_t __buffered_write(FILE *f, const char *src, size_t len)
{
    if (!f || !f->writable || len == 0)
        return 0;

    // Unbuffered, no buffer, or a zero-size buffer: write directly. A zero-size
    // buffer would otherwise make the loop below spin without making progress.
    if (f->buf_mode == _IONBF || !f->wbuf || f->wbuf_size == 0) {
        ssize_t w = write(f->fd, src, len);
        if (w <= 0)
            return 0;
        f->pos += (size_t)w;
        if (f->pos > f->size)
            f->size = f->pos;
        return (size_t)w;
    }

    size_t written = 0;
    while (written < len) {
        size_t avail = f->wbuf_size - f->wbuf_pos;
        size_t chunk = len - written;
        if (chunk > avail)
            chunk = avail;

        memcpy(f->wbuf + f->wbuf_pos, src + written, chunk);
        f->wbuf_pos += chunk;
        written += chunk;
        f->pos += chunk;
        if (f->pos > f->size)
            f->size = f->pos;

        // Buffer full: flush
        if (f->wbuf_pos >= f->wbuf_size) {
            if (__flush_wbuf(f) != 0)
                break;
        }
    }

    // Line-buffered: flush if we wrote any newlines
    if (f->buf_mode == _IOLBF) {
        for (size_t i = len; i > 0; i--) {
            if (src[i - 1] == '\n') {
                __flush_wbuf(f);
                break;
            }
        }
    }

    return written;
}

/// @brief Initialize write buffering fields on a FILE.
static void __init_wbuf(FILE *f)
{
    if (f->writable) {
        f->wbuf      = f->wbuf_static;
        f->wbuf_size = BUFSIZ;
        f->wbuf_pos  = 0;
        f->buf_mode  = isatty(f->fd) ? _IOLBF : _IOFBF;
    } else {
        f->wbuf      = nullptr;
        f->wbuf_size = 0;
        f->wbuf_pos  = 0;
        f->buf_mode  = _IONBF;
    }
}

static int seek_to_position(FILE *f, size_t target, bool for_write)
{
    if (!f)
        return -1;
    if (f->path[0] == '\0')
        return f->fd >= 0 ? 0 : -1;

    // Reopen file if fd is closed
    if (f->fd < 0) {
        int flags = f->open_flags;
        if (for_write && f->append)
            flags |= O_APPEND;

        int fd = open(f->path, flags);
        if (fd < 0) {
            f->fd = -1;
            return -1;
        }
        f->fd = fd;
    }

    // Use lseek for positioning
    size_t seek_target = (for_write && f->append) ? f->size : target;
    long result        = lseek(f->fd, (long)seek_target, SEEK_SET);
    if (result < 0)
        return -1;

    f->pos = seek_target;
    return 0;
}

static int ensure_position(FILE *f, size_t target, bool for_write)
{
    if (!f)
        return -1;

    if (f->data) {
        f->pos       = target;
        f->need_seek = false;
        return 0;
    }

    if (f->path[0] == '\0') {
        f->pos       = target;
        f->need_seek = false;
        return 0;
    }

    if (!f->need_seek && f->fd >= 0 && f->pos == target)
        return 0;

    int res = seek_to_position(f, target, for_write);
    if (res == 0) {
        f->pos       = target;
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

    f->readable  = rd || (!wr && !ap);
    f->writable  = wr;
    f->append    = ap;
    f->need_seek = false;
    if (f->readable && f->writable)
        f->open_flags = O_RDWR;
    else if (f->writable)
        f->open_flags = O_WRONLY;
    else
        f->open_flags = O_RDONLY;
    if (create)
        f->open_flags |= O_CREAT;
    if (trunc && !ap)
        f->open_flags |= O_TRUNC;
    f->pos  = 0;
    f->fd   = (f->open_flags & O_CREAT) ? open(path, f->open_flags, 0644) : open(path, f->open_flags);
    f->data = nullptr;

    if (f->fd < 0) {
        free(f);
        return nullptr;
    }
    if (f->open_flags & O_TRUNC)
        f->open_flags &= ~O_TRUNC;

    struct stat st;
    if (fstat(f->fd, &st) == 0)
        f->size = (size_t)st.st_size;
    else
        f->size = 0;

    __init_wbuf(f);

    if (ap) {
        f->pos = f->size;
        if (f->pos > 0)
            f->need_seek = true;
    } else {
        f->pos = 0;
    }
    return f;
}

FILE *fdopen(int fd, const char *mode)
{
    if (fd < 0 || !mode)
        return nullptr;
    bool rd = false, wr = false, ap = false, trunc = false, create = false;
    parse_mode(mode, &rd, &wr, &ap, &trunc, &create);
    FILE *f = malloc(sizeof(FILE));
    if (!f)
        return nullptr;
    memset(f, 0, sizeof(FILE));
    f->fd         = fd;
    f->path[0]    = '\0';
    f->readable   = rd || (!wr && !ap);
    f->writable   = wr;
    f->append     = ap;
    f->need_seek  = false;
    f->open_flags = (f->readable && f->writable) ? O_RDWR : (f->writable ? O_WRONLY : O_RDONLY);
    f->data       = nullptr;
    struct stat st;
    f->size = (fstat(fd, &st) == 0) ? (size_t)st.st_size : 0;
    f->pos  = 0;
    __init_wbuf(f);
    return f;
}

int fclose(FILE *stream)
{
    if (!stream)
        return -1;
    __flush_wbuf(stream);
    if (stdio_is_standard_stream(stream))
        return 0;
    if (stream->fd >= 0)
        close(stream->fd);
    if (stream->data)
        free(stream->data);
    // Do not free stream->wbuf: it is either wbuf_static (embedded in this
    // struct) or a caller-owned setvbuf buffer. The library never heap-
    // allocates it, so freeing here would free memory it does not own.
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

    // Flush pending writes before reading (POSIX mixed r/w requirement)
    if (stream->wbuf_pos > 0)
        __flush_wbuf(stream);

    if (stream->data) {
        if (stream->pos >= stream->size)
            return 0;
        if (bytes > stream->size - stream->pos)
            bytes = stream->size - stream->pos;
        memcpy(ptr, stream->data + stream->pos, bytes);
        stream->pos += bytes;
        return bytes / size;
    }

    if (stream->path[0] == '\0' && stream->fd >= 0) {
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

    // For file-backed streams with paths, ensure fd is positioned correctly
    if (stream->path[0] != '\0' && stream->data == nullptr) {
        size_t target = stream->append ? stream->size : stream->pos;
        if (ensure_position(stream, target, true) != 0)
            return 0;
    }

    size_t w = __buffered_write(stream, (const char *)ptr, bytes);
    return w / size;
}

int getc_unlocked(FILE *stream)
{
    if (!stream || !stream->readable)
        return EOF;
    unsigned char c;
    if (fread(&c, 1, 1, stream) != 1)
        return EOF;
    return c;
}

int putc_unlocked(int c, FILE *stream)
{
    unsigned char ch = (unsigned char)c;
    return (fwrite(&ch, 1, 1, stream) == 1) ? c : EOF;
}

int fgetc(FILE *stream)
{
    return getc_unlocked(stream);
}

int fputc(int c, FILE *stream)
{
    return putc_unlocked(c, stream);
}

int putchar_unlocked(int c)
{
    return putc_unlocked(c, stdout);
}

int fileno(FILE *stream)
{
    return stream ? stream->fd : -1;
}

int fileno_unlocked(FILE *stream)
{
    return fileno(stream);
}

void clearerr(FILE *stream)
{
    (void)stream;
}

int fputs_unlocked(const char *s, FILE *stream)
{
    if (!s || !stream)
        return EOF;
    size_t len = strlen(s);
    return (fwrite(s, 1, len, stream) == len) ? 0 : EOF;
}

char *fgets_unlocked(char *s, int n, FILE *stream)
{
    if (!s || n <= 0 || !stream)
        return nullptr;
    char *p = s;
    while (n > 1) {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        *p++ = (char)c;
        n--;
        if (c == '\n')
            break;
    }
    *p = '\0';
    return (p > s) ? s : nullptr;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    if (!lineptr || !n || !stream)
        return -1;
    size_t alloc = *n;
    char *buf    = *lineptr;
    if (!buf || alloc < 2) {
        alloc = 128;
        buf   = malloc(alloc);
        if (!buf)
            return -1;
        *lineptr = buf;
        *n       = alloc;
    }
    size_t i = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (i + 2 > alloc) {
            alloc *= 2;
            char *newbuf = realloc(buf, alloc);
            if (!newbuf) {
                if (i > 0) {
                    buf[i] = '\0';
                    *n     = alloc / 2;
                    return (ssize_t)i;
                }
                return -1;
            }
            buf      = newbuf;
            *lineptr = buf;
            *n       = alloc;
        }
        buf[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0 && c == EOF)
        return -1;
    buf[i] = '\0';
    return (ssize_t)i;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream)
        return -1;
    __flush_wbuf(stream);
    size_t newpos = 0;
    switch (whence) {
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
    stream->pos       = newpos;
    stream->need_seek = (stream->data == nullptr && stream->path[0] != '\0');
    return 0;
}

int fseeko(FILE *stream, off_t offset, int whence)
{
    return fseek(stream, (long)offset, whence);
}

long ftell(FILE *stream)
{
    if (!stream)
        return -1;
    return (long)stream->pos;
}

off_t ftello(FILE *stream)
{
    if (!stream)
        return -1;
    return (off_t)stream->pos;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    if (!path || !mode || !stream)
        return nullptr;
    __flush_wbuf(stream);
    if (stream->fd >= 0 && !stdio_is_standard_stream(stream))
        close(stream->fd);
    stream->fd = -1;
    if (stream->data) {
        free(stream->data);
        stream->data = nullptr;
    }
    bool rd = false, wr = false, ap = false, trunc = false, create = false;
    parse_mode(mode, &rd, &wr, &ap, &trunc, &create);
    stream->readable   = rd || (!wr && !ap);
    stream->writable   = wr;
    stream->append     = ap;
    stream->need_seek  = false;
    stream->open_flags = (stream->readable && stream->writable) ? O_RDWR : (stream->writable ? O_WRONLY : O_RDONLY);
    if (create)
        stream->open_flags |= O_CREAT;
    if (trunc && !ap)
        stream->open_flags |= O_TRUNC;
    strncpy(stream->path, path, sizeof(stream->path) - 1);
    stream->path[sizeof(stream->path) - 1] = '\0';
    stream->fd = (stream->open_flags & O_CREAT) ? open(path, stream->open_flags, 0644) : open(path, stream->open_flags);
    if (stream->fd < 0)
        return nullptr;
    if (stream->open_flags & O_TRUNC)
        stream->open_flags &= ~O_TRUNC;
    struct stat st;
    stream->size = (fstat(stream->fd, &st) == 0) ? (size_t)st.st_size : 0;
    stream->pos  = ap ? stream->size : 0;
    if (ap && stream->pos > 0)
        stream->need_seek = true;
    __init_wbuf(stream);
    return stream;
}

int ferror(FILE *stream)
{
    (void)stream;
    return 0;
}

int ferror_unlocked(FILE *stream)
{
    (void)stream;
    return 0;
}

int fflush(FILE *stream)
{
    if (!stream) {
        return stdio_flush_standard_outputs();
    }
    return __flush_wbuf(stream);
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    if (!stream || (mode != _IONBF && mode != _IOLBF && mode != _IOFBF))
        return -1;
    __flush_wbuf(stream);
    stream->buf_mode = mode;
    if (mode == _IONBF) {
        stream->wbuf      = nullptr;
        stream->wbuf_size = 0;
    } else if (buf) {
        stream->wbuf      = buf;
        stream->wbuf_size = size;
    } else {
        stream->wbuf      = stream->wbuf_static;
        stream->wbuf_size = (size <= BUFSIZ) ? size : BUFSIZ;
    }
    stream->wbuf_pos = 0;
    return 0;
}

void setbuf(FILE *stream, char *buf)
{
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
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
