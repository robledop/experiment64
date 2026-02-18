# Shared Memory

Named shared memory regions allow unrelated processes to map the same physical
pages into their address spaces.

## Syscalls

### `SYS_SHM_OPEN` (51)

```c
int shm_open(const char *name, int flags, size_t size);
```

Creates or opens a named shared memory object and returns a file descriptor.

- `name`: identifier (max 63 characters).
- `flags`: `O_CREATE` to create a new object (fails with `-EINSTKN` if the
  name already exists). Omit `O_CREATE` to open an existing one (fails with
  `-ENOENT` if not found).
- `size`: byte size of the region (only meaningful when creating; must be
  non-zero).

The returned fd can be passed to `mmap` with `MAP_SHARED` to map the region.
Physical pages are allocated at creation time and zeroed.

### `SYS_SHM_UNLINK` (52)

```c
int shm_unlink(const char *name);
```

Marks a shared memory object for deletion. If no file descriptors reference it,
the object and its physical pages are freed immediately. Otherwise, cleanup is
deferred until the last fd is closed.

## mmap integration

```c
int fd = shm_open("my_region", O_CREATE, 4096);
void *ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
```

The mapped pages are not freed by `munmap`; they belong to the shared memory
object and persist until it is unlinked and all references are closed.

## Limits

- `SHM_MAX_OBJECTS`: 32 concurrent shared memory objects.
- `SHM_NAME_MAX`: 64-byte name buffer (63 usable characters).

## Kernel internals

- Registry: `kernel/ipc/shm.c`, header `include/ipc/shm.h`.
- Syscalls: `kernel/syscalls/sys_shm.c`.
- Each shm object holds an array of physical page addresses. `mmap` maps these
  into the caller's address space page-by-page.
- The inode returned by `shm_open` uses `shm_inode_ops`; `sys_mmap` checks
  `shm_is_shm_inode()` to dispatch to the shared memory mapping path.
- Reference counting tracks open file descriptors. The inode `close` handler
  calls `shm_unref`, which frees the object when marked for unlink and the
  refcount reaches zero.
