#include <lib/path.h>
#include <status.h>
#include <syscall_common.h>

int split_parent_path(const char *path, char *parent, size_t parent_size)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
        return -EBADPATH;
    if (last_slash == path) {
        if (last_slash[1] == '\0')
            return -EBADPATH;
        path_safe_copy(parent, parent_size, "/");
        return ALL_OK;
    }

    const size_t len = (size_t)(last_slash - path);
    if (len >= parent_size || last_slash[1] == '\0')
        return -EBADPATH;

    strncpy(parent, path, len);
    parent[len] = '\0';
    return ALL_OK;
}

void fill_stat_from_inode(const vfs_inode_t *inode, struct stat *st)
{
    if (!inode || !st)
        return;

    if (inode->iops && inode->iops->stat) {
        if (inode->iops->stat(inode, st) == 0)
            return;
    }

    st->dev     = 0;
    st->ino     = (int)inode->inode;
    st->type    = (int)(inode->flags & VFS_TYPE_MASK);
    st->st_mode = vfs_mode_from_type((uint32_t)st->type);
    st->nlink   = 1;
    st->size    = inode->size;
    st->ref     = 0;
    st->i_atime = 0;
    st->i_ctime = 0;
    st->i_mtime = 0;
    st->i_dtime = 0;
    st->i_uid   = 0;
    st->i_gid   = 0;
    st->i_flags = 0;
}

void set_process_name_from_path(process_t *proc, const char *path)
{
    if (!proc || !path)
        return;

    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' && p[1])
            name = p + 1;
    }

    path_safe_copy(proc->name, sizeof(proc->name), name);
}

int resolve_user_path(const char *path, char *resolved, size_t size)
{
    if (!resolved || size == 0)
        return -1;

    char input_buf[PATH_MAX];
    size_t max_len = size < sizeof(input_buf) ? size : sizeof(input_buf);
    if (!copy_from_user_str(input_buf, path, max_len))
        return -1;
    if (input_buf[0] == '\0')
        return -1;

    const char *base = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";
    path_build_absolute(base, input_buf, resolved, size);
    return 0;
}

int resolve_user_path_checked(const char *path, char *resolved, size_t size, const char *op)
{
    int status = require_user_ptr_read(path, 1, op, -EFAULT);
    if (status != 0)
        return status;

    return resolve_user_path(path, resolved, size) == 0 ? ALL_OK : -EBADPATH;
}
