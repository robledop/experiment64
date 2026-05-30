# open() to the first bytes read off disk

This walkthrough traces what happens between a userland `open("/some/path")`
and the first `read()` that returns file contents, for a file living on the
ext2 root filesystem. It follows the four layers the request passes through —
syscall glue, the VFS dispatch table, the ext2 driver, and the block cache —
and points out where data actually leaves the disk. Understanding this path is
the quickest way to see how an inode-based VFS, a vtable of `iops`, and a
write-back buffer cache fit together in this tree.

## Scope

This covers the **read** path on the **ext2** mount at `/` only. FAT32
(`/mnt`, `/boot`) has its own driver with the same `iops` shape but different
internals. Write, truncate, mknod, and directory enumeration share the same
machinery but are not traced here. Symlinks are not resolved by the path
walker. The block cache is a fixed-size array with no read-ahead.

## Files in play

- `kernel/syscalls/sys_open.c` — `sys_open()`: resolves the path to a VFS
  inode and binds it to a file descriptor.
- `kernel/syscalls/sys_read.c` — `sys_read()`: validates the fd and forwards
  to `vfs_read()` using the descriptor's current offset.
- `kernel/fs/vfs.c` — VFS core. `vfs_resolve_path()` walks the path component
  by component, `vfs_finddir()` dispatches one lookup, `vfs_read()` dispatches
  the read through the inode's `iops` vtable.
- `kernel/fs/ext2.c` — the ext2 driver behind the vtable: `ext2_vfs_finddir`,
  `ext2fs_dirlookup`, `iget`, `ext2fs_ilock`, `ext2fs_bmap`, `ext2_read_inode`,
  `ext2_vfs_read`.
- `include/io/bio.h` / `kernel/io/bio.c` — the block cache. `bread()` returns
  a 512-byte buffer, reading from the storage device only on a cache miss.
- `include/fs/ext2.h` — on-disk constants (`EXT2_BSIZE`, `EXT2_NDIR_BLOCKS`,
  `NINODE`, indirect-block layout).

## The walk

### Path resolution (open)

1. **`sys_open()` canonicalizes the path.** `sys_open.c:13` copies the user
   string into a kernel buffer and resolves it to an absolute path via
   `resolve_user_path_checked()`. Then `sys_open.c:16` calls
   `vfs_resolve_path(abs_path)`. If that returns nothing and `O_CREAT` is set,
   it tries `vfs_mknod` and resolves again (`sys_open.c:17-21`).

2. **`vfs_resolve_path()` walks one component at a time.** The real work is in
   `vfs_resolve_path_hold()` (`vfs.c:260`). It starts at the global
   `vfs_root` (`vfs.c:265`), skips leading slashes, then chops the path on `/`
   and calls `vfs_finddir(current, name)` for each component
   (`vfs.c:279`, `vfs.c:296`). Each successful lookup releases the old
   `current` and advances to the child. A bare `/` short-circuits and returns
   `vfs_root` directly (`vfs.c:269-270`).

3. **`vfs_finddir()` dispatches through the vtable.** `vfs.c:229` checks the
   node is a directory with a `finddir` op, gives the mount table first refusal
   when the node is `vfs_root` (`vfs.c:236-240`, so `/dev`, `/mnt` etc. resolve
   to their mounted roots), then calls `node->iops->finddir(node, name)`
   (`vfs.c:242`). For the ext2 root that lands in `ext2_vfs_finddir`.

4. **`ext2_vfs_finddir()` scans the directory and wraps the result.**
   `ext2.c:1451`. It takes the per-device lock, locks the *parent* directory
   inode (`ext2fs_ilock(dp)`, `ext2.c:1463`), then calls
   `ext2fs_dirlookup(dp, name, nullptr)` (`ext2.c:1473`).

5. **`ext2fs_dirlookup()` reads directory entries until the name matches.**
   `ext2.c:967`. It walks the directory's data by repeatedly calling
   `ext2_read_inode()` (`ext2.c:974`, `ext2.c:989`) — the same byte-level read
   helper used for ordinary files — comparing each `name` against the target.
   On a hit it returns `iget(dp->dev, de.inode)` (`ext2.c:1000`).

6. **`iget()` only *reserves a cache slot*. No disk I/O happens here.**
   `ext2.c:145`. Under `icache.lock` it scans the `NINODE`-entry (`= 50`,
   `ext2.h:45`) inode cache for a live entry with the same `(dev, inum)`; if
   found it bumps `ref` and returns. Otherwise it claims an empty slot, stamps
   `dev`/`inum`, sets `ref = 1`, and crucially sets **`ip->valid = 0`**
   (`ext2.c:177`). The on-disk inode (mode, size, block pointers) has *not*
   been loaded yet. This is the lazy-load split that surprises most readers:
   `iget` is a cache reservation, not a read.

7. **The matching `ext2fs_ilock()` is what reads the inode off disk.** Back in
   `ext2_vfs_finddir`, `ext2.c:1482` immediately calls `ext2fs_ilock(ip)` on
   the inode `dirlookup` just returned. `ext2fs_ilock` (`ext2.c:583`) takes the
   inode's sleeplock and, only when `ip->valid == 0` (`ext2.c:597`), locates
   the inode's slot with `ext2_open_inode_slot()` → `bread()`, copies the raw
   `struct ext2_disk_inode` out, fills in `type`/`size`/`i_block[]` addresses,
   and sets `ip->valid = 1` (`ext2.c:604-639`). So for a freshly looked-up
   file, the on-disk inode is loaded *here, during open's path walk* — not
   during the later `read()`. A second `ilock` on the same cached inode is a
   no-op fast path because `valid` is already 1.

8. **`ext2_vfs_finddir` builds the VFS inode.** With the ext2 inode loaded, it
   `kmalloc`s a `vfs_inode_t`, copies `inum`/`size`, sets `flags` to
   `VFS_DIRECTORY` or `VFS_FILE` from `ip->type`, stashes the `struct
   ext2_inode *` in `node->device`, and points `node->iops` at the shared
   `ext2_vfs_ops` table (`ext2.c:1488-1501`). That `vfs_inode_t` is what bubbles
   back up to `sys_open`.

9. **`sys_open` binds the inode to a descriptor.** `fd_alloc()` (`sys_open.c:49`)
   wraps the inode in a `file_descriptor_t`, `O_APPEND` seeds the offset to
   `inode->size` (`sys_open.c:56-57`), `fd_assign()` puts it in the fd table at
   the first free slot ≥ 3 (`sys_open.c:58`), `vfs_open()` runs the inode's
   `open` hook (`sys_open.c:66`; ext2's is a no-op), and the integer fd is
   returned.

### The read

10. **`sys_read()` validates and forwards.** `sys_read.c`: it bounds-checks the
    fd, requires `count > 0` and a writable user buffer (`sys_read.c:7-12`),
    fetches the descriptor with `fd_get()`, confirms it is readable
    (`fd_can_read`, `sys_read.c:21`), then calls
    `vfs_read(desc->inode, desc->offset, count, buf)` (`sys_read.c:26`). The
    bytes actually read are added to `desc->offset` (`sys_read.c:27`) so the
    next `read()` continues where this one stopped.

11. **`vfs_read()` dispatches through the same vtable.** `vfs.c:70`. If the
    inode has a `read` op it calls it; otherwise it returns 0. For ext2 that is
    `ext2_vfs_read`.

12. **`ext2_vfs_read()` locks the inode and delegates.** `ext2.c:1126`. It
    pulls the `struct ext2_inode *` back out of `node->device`, calls
    `ext2fs_ilock(ip)` (`ext2.c:1134` — usually the no-op fast path now, since
    `open` already loaded the inode), then `ext2_read_inode()`, then
    `ext2fs_iunlock`.

13. **`ext2_read_inode()` clamps to file size and copies sector by sector.**
    `ext2.c:868`. It rejects reads past `ip->size` and shrinks `n` so a read
    can never run off the end of the file (`ext2.c:874-879`). Then it loops:
    compute the *logical* block number `off / EXT2_BSIZE`
    (`EXT2_BSIZE = 1024`, `ext2.h:11`), translate it with
    `ext2fs_bmap(ip, logical_block)` (`ext2.c:883`), add the in-block sector
    offset, `bread()` that 512-byte sector (`ext2.c:893`), and `memcpy` the
    relevant slice into the user buffer (`ext2.c:902`). Note that one ext2
    block is two 512-byte sectors, so a single 1024-byte logical block can take
    two cache buffers across loop iterations.

14. **`ext2fs_bmap()` turns a logical block into an absolute disk sector.**
    `ext2.c:759`. This is the indirect-block resolver:
    - `bn < EXT2_NDIR_BLOCKS` (`= 12`, `ext2.h:21`): the sector lives in the
      inode's direct pointer array `ad->addrs[bn]` (`ext2.c:763-769`).
    - else subtract 12; if it fits in one indirect block
      (`EXT2_INDIRECT = EXT2_BSIZE/4 = 256` entries) follow
      `ad->addrs[EXT2_IND_BLOCK]` via `ext2_ensure_ptr` (`ext2.c:772-781`).
    - else double-indirect, then triple-indirect, each adding one more level of
      `ext2_ensure_ptr` (`ext2.c:785-826`).

    In every branch the return value is
    `BLOCK_TO_SECTOR(block) + ext2_part_offset(ip->dev)`:
    `BLOCK_TO_SECTOR` multiplies by `EXT2_BSIZE/512 = 2` (`ext2.c:48`), and
    `ext2_part_offset` adds the partition's start LBA (`ext2.c:59`) so the
    sector number is absolute on the whole disk, not relative to the partition.

15. **`bread()` is where bytes finally leave the disk — on a miss.**
    `bio.c:163`. Under `bio_lock` it calls `get_blk()` (`bio.c:48`) to find or
    LRU-recycle a 512-byte buffer for `(device, block)`. It then drops the
    spinlock, takes the buffer's sleeplock, and **only if `BIO_FLAG_VALID` is
    clear** issues the real device read via `storage_read()` and sets the flag
    (`bio.c:185-202`). A cache hit skips `storage_read` entirely. The buffer is
    returned locked; `ext2_read_inode` copies out of `bp->data` and calls
    `brelse()` (`ext2.c:902-903`) to unlock and drop the ref count
    (`bio.c:227`).

So the *very first* physical sector read for a fresh open usually happens in
step 7 (loading the inode during the path walk), and the *first file-content*
sector is read in step 15, inside the `read()` call, gated by `BIO_FLAG_VALID`.

## Gotchas

- **`iget` does not touch the disk; the matching `ilock` does.** `iget`
  (`ext2.c:145`) only reserves a cache slot with `valid = 0`. The disk inode is
  loaded by the first `ext2fs_ilock` that sees `valid == 0` (`ext2.c:597`).
  Searching for "where is the inode read" by looking at `iget` will mislead you.

- **The lazy inode load fires during *open*, not *read*.** Because
  `ext2_vfs_finddir` calls `ext2fs_ilock(ip)` right after `dirlookup`
  (`ext2.c:1482`), the inode is already `valid` by the time `read()` runs, so
  the `ilock` inside `ext2_vfs_read` (`ext2.c:1134`) is normally a no-op
  fast path. The "lazy" load is real, but it is triggered earlier than the
  name `read` suggests.

- **`bmap` returns a *sector*, not a block, and it is partition-absolute.**
  Despite operating on logical ext2 blocks, every return path goes through
  `BLOCK_TO_SECTOR(...) + ext2_part_offset(...)` (`ext2.c:769` et al.), so the
  value handed to `bread` is an absolute LBA. Mixing up "block" and "sector"
  here is the easiest off-by-2 in the driver.

- **`bmap` allocates on read.** On a path where a block pointer is 0,
  `ext2fs_bmap` calls `ext2fs_balloc`/`ext2_ensure_ptr` to *allocate* a block
  (`ext2.c:764-767`) even when reached from the read path. For a normal read of
  existing data the pointers are already non-zero, so this never fires — but
  the function is not read-only by construction.

- **One ext2 block spans two cache buffers.** `EXT2_BSIZE` is 1024 but the
  block cache (`BIO_BLOCK_SIZE`, `bio.h:7`) and `storage_read` deal in 512-byte
  sectors. `ext2_read_inode` therefore issues `bread` per 512-byte sector and
  may loop twice to cover a single logical block (`ext2.c:905-906`).

- **`vfs_read` silently returns 0 when there is no `read` op.** The vtable
  dispatch (`vfs.c:72-74`) treats a missing op as "0 bytes," not an error, so a
  short or zero read can come from a missing `iops` slot rather than EOF.

## See also

- `docs/ext2.md` — the on-disk ext2 layout and driver internals.
- `docs/storage.md` — what `storage_read` sits on top of (IDE/AHCI backends).
- `docs/syscalls.md` — syscall entry, dispatch, and fd-table mechanics.
- `docs/address_space.md` — user-pointer validation referenced in `sys_read`.
- Acronyms used here (VFS, LBA, LRU, ext2 inode/block terms) are collected in
  `docs/glossary.md`.
