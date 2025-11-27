#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define FMTNAME_WIDTH 14
#define PATHBUF_SZ 512

char *fmtname(char *path)
{
    static char buf[FMTNAME_WIDTH + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--) {
    }
    p++;

    // Return blank-padded name.
    if (strlen(p) >= FMTNAME_WIDTH)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', FMTNAME_WIDTH - strlen(p));
    buf[FMTNAME_WIDTH] = 0;
    return buf;
}

struct ls_ctx
{
    char path[PATHBUF_SZ];
    int base_len;
};

char *bytes_to_human_readable(uint32_t bytes, char *output, uint32_t output_size)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    uint32_t unit_index      = 0;
    double size         = (double)bytes;

    while (size >= 1024 && unit_index < sizeof(units) / sizeof(units[0]) - 1) {
        size /= 1024;
        unit_index++;
    }

    snprintf(output, output_size, "%.2f %s", size, units[unit_index]);
    return output;
}

static void print_entry(char *name, struct stat *st)
{
    switch (st->type) {
    case T_DIR:
        printf(KWHT "d ");
        break;
    case T_DEV:
        printf(KWHT "c ");
        break;
    default:
        printf(KWHT "- ");
    }

    printf(" %5d ", st->ino);

    char human_readable_size[20];
    bytes_to_human_readable(st->size, human_readable_size, sizeof(human_readable_size));
    printf(" %10s ", human_readable_size);

    struct tm modify_time = {0};
    unix_timestamp_to_tm(st->i_mtime, &modify_time);

    const char *date_time_format = "%Y %B %d %H:%M";
    char modify_time_str[25]     = {0};

    strftime(date_time_format, &modify_time, modify_time_str, sizeof(modify_time_str));
    printf(" %s ", modify_time_str);

    switch (st->type) {
    case T_DIR:
        printf(KBBLU " %s\n" KWHT, name);
        break;
    case T_DEV:
        printf(KYEL " %s\n" KWHT, name);
        break;
    default:
        printf(KWHT " %s\n", name);
    }
}

static int ls_visit(const struct dirent_view *entry, void *arg)
{
    struct ls_ctx *ctx = (struct ls_ctx *)arg;

    if (ctx->base_len + entry->name_len + 1 >= PATHBUF_SZ) {
        printf("ls: path too long\n");
        return 0;
    }

    memmove(ctx->path + ctx->base_len, entry->name, entry->name_len);
    ctx->path[ctx->base_len + entry->name_len] = 0;

    struct stat st;
    if (stat(ctx->path, &st) < 0) {
        printf("ls: cannot stat %s\n", ctx->path);
        return -1;
    }

    print_entry(fmtname(ctx->path), &st);
    return 0;
}

void ls(char *path)
{
    int fd;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        printf("ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        printf("ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        print_entry(fmtname(path), &st);
        break;

    case T_DIR: {
        struct ls_ctx ctx;
        if (strlen(path) + 1 + EXT2_DIRENT_NAME_MAX + 1 > sizeof(ctx.path)) {
            printf("ls: path too long\n");
            break;
        }
        strcpy(ctx.path, path);
        ctx.base_len = (int)strlen(ctx.path);
        if (ctx.base_len == 0 || ctx.path[ctx.base_len - 1] != '/') {
            ctx.path[ctx.base_len++] = '/';
            ctx.path[ctx.base_len]   = 0;
        }
        if (dirwalk(fd, ls_visit, &ctx) < 0)
            printf("ls: cannot read directory %s\n", path);
        break;
    }
    default: ;
        printf("ls: unknown type %d for %s\n", st.type, path);
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        ls(".");
        exit();
    }
    for (int i = 1; i < argc; i++) {
        ls(argv[i]);
    }
}
