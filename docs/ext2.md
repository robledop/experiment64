# ext2

This file documents only the on-disk **permission defaults**. The ext2 driver
design — the inode cache, the lazy `iget`/`ilock` load, `bmap`, and the VFS
vtable it installs — lives in `kernel/fs/ext2.c` (see the comment at the top of
that file) and is traced end to end in
[walkthroughs/04-open-to-disk-bytes.md](walkthroughs/04-open-to-disk-bytes.md).

## Default permissions

New directories are created with mode 0755 and regular files with 0644. 
This is just so we can easly access the file system if we mount it on Linux. This OS does not yet support permissions or even users.
