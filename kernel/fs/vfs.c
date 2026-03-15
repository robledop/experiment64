#include <fs/vfs.h>
#include <lib/path.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <stddef.h>
#include <status.h>

vfs_inode_t *vfs_root = nullptr;

void vfs_init()
{
    vfs_root = nullptr;
}

static void vfs_put_inode(vfs_inode_t *node)
{
    if (!node || node == vfs_root)
        return;
    vfs_close(node);
    kfree(node);
}

void vfs_release(vfs_inode_t *node)
{
    if (!node || node == vfs_root)
        return;

    if (node->ref > 1) {
        node->ref--;
        return;
    }

    vfs_put_inode(node);
}

uint64_t vfs_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->iops && node->iops->read)
        return node->iops->read(node, offset, size, buffer);
    return 0;
}

uint64_t vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (node->iops && node->iops->write)
        return node->iops->write(node, offset, size, buffer);
    return 0;
}

int vfs_truncate(vfs_inode_t *node)
{
    if (node->iops && node->iops->truncate)
        return node->iops->truncate(node);
    return -1;
}

int vfs_ioctl(vfs_inode_t *node, int request, void *arg)
{
    if (node->iops && node->iops->ioctl)
        return node->iops->ioctl(node, request, arg);
    return -1;
}

void vfs_open(const vfs_inode_t *node)
{
    if (node->iops && node->iops->open)
        node->iops->open(node);
}

void vfs_close(vfs_inode_t *node)
{
    if (node->iops && node->iops->close)
        node->iops->close(node);
}

int vfs_poll(const vfs_inode_t *node, short events, short *revents)
{
    if (!node || !revents)
        return -1;
    if (node->iops && node->iops->poll)
        return node->iops->poll(node, events, revents);
    return -1;
}

static void vfs_put_inode_hold(vfs_inode_t *node, const vfs_inode_t *hold)
{
    if (!node || node == hold)
        return;
    vfs_release(node);
}

static vfs_inode_t *vfs_resolve_path_hold(const char *path, const vfs_inode_t *hold)
{
    if (!path || !vfs_root)
        return nullptr;

    vfs_inode_t *current = vfs_root;

    // Handle absolute paths (treat as relative to root for now)
    while (*path == '/')
        path++;

    if (*path == 0)
        return current; // Root

    char name[128]; // Max filename length
    int name_idx = 0;

    while (*path) {
        if (*path == '/') {
            name[name_idx] = 0;
            if (name_idx > 0) {
                vfs_inode_t *next = vfs_finddir(current, name);
                if (!next) {
                    vfs_put_inode_hold(current, hold);
                    return nullptr;
                }
                vfs_put_inode_hold(current, hold);
                current = next;
            }
            name_idx = 0;
        } else {
            if (name_idx < 127)
                name[name_idx++] = *path;
        }
        path++;
    }

    // Last component
    if (name_idx > 0) {
        name[name_idx]    = 0;
        vfs_inode_t *next = vfs_finddir(current, name);
        if (!next) {
            vfs_put_inode_hold(current, hold);
            return nullptr;
        }
        vfs_put_inode_hold(current, hold);
        current = next;
    }

    return current;
}

vfs_inode_t *vfs_resolve_path(const char *path)
{
    return vfs_resolve_path_hold(path, nullptr);
}

static int vfs_normalize_status(int status)
{
    if (status == 0)
        return 0;
    if (status < 0)
        return (status == -1) ? -EIO : status;
    return -EIO;
}

static int vfs_split_parent_name(const char *path, char *parent, size_t parent_size,
                                 char *name, size_t name_size)
{
    if (!path || !parent || !name || parent_size == 0 || name_size == 0)
        return -EINVAL;
    if (path[0] == '\0')
        return -EBADPATH;

    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        ptrdiff_t len = last_slash - path;
        if (len <= 0) {
            path_safe_copy(parent, parent_size, "/");
        } else {
            if ((size_t)len >= parent_size)
                return -EBADPATH;
            strncpy(parent, path, (size_t)len);
            parent[len] = '\0';
        }

        const char *base = last_slash + 1;
        if (base[0] == '\0' || strlen(base) >= name_size)
            return -EBADPATH;
        path_safe_copy(name, name_size, base);
        return ALL_OK;
    }

    path_safe_copy(parent, parent_size, "/");
    if (strlen(path) >= name_size)
        return -EBADPATH;
    path_safe_copy(name, name_size, path);
    return ALL_OK;
}

int vfs_mknod(char *path, int mode, int dev)
{
    if (!path)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(path, "/") == 0)
        return -EPERM;

    char parent_path[PATH_MAX];
    char filename[128];
    int split_status = vfs_split_parent_name(path, parent_path, sizeof(parent_path), filename, sizeof(filename));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *existing = vfs_resolve_path(path);
    if (existing) {
        vfs_release(existing);
        return -EINSTKN;
    }

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent)
        return -ENOENT;

    int res = ALL_OK;
    if ((parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!parent->iops || !parent->iops->mknod) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(parent->iops->mknod(parent, filename, mode, dev));
    }

    vfs_release(parent);
    return res;
}

int vfs_link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(newpath, "/") == 0)
        return -EPERM;

    vfs_inode_t *target = vfs_resolve_path(oldpath);
    if (!target)
        return -ENOENT;
    if ((target->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
        vfs_release(target);
        return -EPERM;
    }

    vfs_inode_t *existing = vfs_resolve_path(newpath);
    if (existing) {
        vfs_release(existing);
        vfs_release(target);
        return -EINSTKN;
    }

    char parent_path[PATH_MAX];
    char filename[128];
    int split_status = vfs_split_parent_name(newpath, parent_path, sizeof(parent_path), filename, sizeof(filename));
    if (split_status != 0) {
        vfs_release(target);
        return split_status;
    }

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent) {
        vfs_release(target);
        return -ENOENT;
    }

    int res = ALL_OK;
    if ((parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!parent->iops || !parent->iops->link || parent->iops != target->iops) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(parent->iops->link(parent, filename, target));
    }

    vfs_release(parent);
    vfs_release(target);
    return res;
}

int vfs_unlink(const char *path)
{
    if (!path)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(path, "/") == 0)
        return -EPERM;

    char parent_path[PATH_MAX];
    char filename[128];
    int split_status = vfs_split_parent_name(path, parent_path, sizeof(parent_path), filename, sizeof(filename));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *target = vfs_resolve_path(path);
    if (!target)
        return -ENOENT;
    if ((target->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
        vfs_release(target);
        return -EISDIR;
    }
    vfs_release(target);

    vfs_inode_t *parent = vfs_resolve_path(parent_path);
    if (!parent)
        return -ENOENT;

    int res = ALL_OK;
    if ((parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!parent->iops || !parent->iops->unlink) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(parent->iops->unlink(parent, filename));
    }

    vfs_release(parent);
    return res;
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;
    if (!vfs_root)
        return -EIO;
    if (strcmp(oldpath, "/") == 0 || strcmp(newpath, "/") == 0)
        return -EPERM;

    char old_parent_path[PATH_MAX];
    char old_name[128];
    int split_status = vfs_split_parent_name(oldpath,
                                             old_parent_path,
                                             sizeof(old_parent_path),
                                             old_name,
                                             sizeof(old_name));
    if (split_status != 0)
        return split_status;

    char new_parent_path[PATH_MAX];
    char new_name[128];
    split_status = vfs_split_parent_name(newpath,
                                         new_parent_path,
                                         sizeof(new_parent_path),
                                         new_name,
                                         sizeof(new_name));
    if (split_status != 0)
        return split_status;

    vfs_inode_t *old_node = vfs_resolve_path(oldpath);
    if (!old_node)
        return -ENOENT;
    if ((old_node->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
        vfs_release(old_node);
        return -EPERM;
    }
    vfs_release(old_node);

    vfs_inode_t *new_node = vfs_resolve_path(newpath);
    if (new_node) {
        if ((new_node->flags & VFS_TYPE_MASK) == VFS_DIRECTORY) {
            vfs_release(new_node);
            return -EISDIR;
        }
        vfs_release(new_node);
    }

    if (strcmp(oldpath, newpath) == 0)
        return 0;

    vfs_inode_t *new_parent = vfs_resolve_path(new_parent_path);
    if (!new_parent)
        return -ENOENT;

    vfs_inode_t *new_parent_hold = new_parent;
    if (new_parent_hold != vfs_root) {
        if (new_parent_hold->iops && new_parent_hold->iops->clone) {
            new_parent = new_parent_hold->iops->clone(new_parent_hold);
        } else {
            new_parent = kmalloc(sizeof(vfs_inode_t));
            if (new_parent)
                memcpy(new_parent, new_parent_hold, sizeof(vfs_inode_t));
        }

        vfs_release(new_parent_hold);
        if (!new_parent)
            return -ENOMEM;
    }

    vfs_inode_t *old_parent = vfs_resolve_path_hold(old_parent_path, new_parent);
    if (!old_parent) {
        vfs_release(new_parent);
        return -ENOENT;
    }

    int res = ALL_OK;
    if ((old_parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY || (new_parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY) {
        res = -ENOTDIR;
    } else if (!old_parent->iops || old_parent->iops != new_parent->iops || !old_parent->iops->rename) {
        res = -ENOTSUP;
    } else {
        res = vfs_normalize_status(old_parent->iops->rename(old_parent, old_name, new_parent, new_name));
    }

    vfs_release(old_parent);
    if (new_parent != old_parent)
        vfs_release(new_parent);
    return res;
}
