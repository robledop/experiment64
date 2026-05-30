/**
 * @file ext2.c
 * @brief ext2 filesystem driver: VFS vtable + xv6-style inode cache.
 *
 * ext2_mount() reads the superblock, brings up the inode cache (icache), and
 * returns a root vfs_inode_t whose ->iops points at ext2_vfs_ops — the
 * inode_operations vtable (read/write/lookup/readdir/mknod/link/unlink/rename/
 * etc., defined near the bottom of this file) through which the VFS layer
 * (kernel/fs/vfs.c) drives this filesystem.
 *
 * Inode lifecycle (xv6-derived) is the part worth understanding first:
 *   - iget(dev, inum)      reserves/recycles an icache slot and bumps ->ref,
 *                          but does NOT touch the disk; the slot is valid == 0.
 *   - ext2fs_ilock(ip)     takes ip->lock and, the first time (valid == 0),
 *                          LAZILY loads the on-disk inode into the slot.
 *   - ext2fs_iput/iunlock  release the reference / sleeplock.
 * So a cached, referenced inode may exist with no disk contents loaded until
 * the first ilock — keep that in mind when reading ->size/->type/->addrs.
 *
 * All disk access goes through the io/bio block cache: bread() to fetch a
 * buffer_head_t, brelse() to release it (see kernel/io/bio.c).
 */
#include <debug.h>
#include <drivers/terminal.h>
#include <fs/ext2.h>
#include <fs/vfs.h>
#include <io/bio.h>
#include <lib/assert.h>
#include <lib/string.h>
#include <lib/util.h>
#include <limits.h>
#include <mem/heap.h>
#include <status.h>
#include <stdint.h>
#include <task/sleeplock.h>
#include <task/spinlock.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Map internal types to VFS types
#define T_DIR EXT2_FT_DIR
#define T_FILE EXT2_FT_REG_FILE
#define T_DEV EXT2_FT_CHRDEV

#define BLOCK_TO_SECTOR(b) ((b) * (EXT2_BSIZE / 512))
#define PTRS_PER_SECTOR (512 / sizeof(uint32_t))

static void ext2fs_bzero(uint32_t dev, uint32_t bno);
static uint32_t ext2fs_balloc(uint32_t dev, uint32_t inum);
static void ext2fs_bfree(uint32_t dev, uint32_t b);
static uint32_t ext2fs_bmap(const struct ext2_inode *ip, uint32_t bn);
static void ext2fs_itrunc(struct ext2_inode *ip);
static uint32_t first_partition_blocks[4] = {0};

// Small helpers to keep dir/alloc code readable.
static inline uint32_t ext2_part_offset(uint32_t dev)
{
    return (dev < 4) ? first_partition_blocks[dev] : 0;
}

static inline uint16_t ext2_dirent_size(u8 name_len)
{
    const uint16_t size = 8 + name_len;
    return (uint16_t)((size + 3) & ~3);
}

struct ext2fs_addrs ext2fs_addrs[NINODE];
static struct ext2_super_block ext2_sb[4]; // Per-device superblock
static sleeplock_t ext2_dev_lock[4];       // Serialize per-device mutations
static bool ext2_dev_lock_initialized = false;

// Helper to get the superblock for a device
static inline struct ext2_super_block *ext2_get_sb(uint32_t dev)
{
    return (dev < 4) ? &ext2_sb[dev] : &ext2_sb[0];
}

static void ext2_dev_locks_init(void)
{
    if (ext2_dev_lock_initialized)
        return;
    for (int i = 0; i < 4; i++) {
        sleeplock_init(&ext2_dev_lock[i], "ext2dev");
    }
    ext2_dev_lock_initialized = true;
}

static inline void ext2_lock(uint32_t dev)
{
    if (!ext2_dev_lock_initialized)
        ext2_dev_locks_init();
    if (dev < 4)
        sleeplock_acquire(&ext2_dev_lock[dev]);
}

static inline void ext2_unlock(uint32_t dev)
{
    if (dev < 4)
        sleeplock_release(&ext2_dev_lock[dev]);
}

void ext2fs_readsb(int dev, struct ext2_super_block *sb)
{
    // Superblock starts at byte 1024 (Sector 2)
    const uint32_t sb_blockno = ext2_part_offset(dev) + 2;

    buffer_head_t *bp = bread(dev, sb_blockno);
    if (!bp || !bp->data) {
        panic("ext2fs_readsb: failed to read superblock");
    }
    memcpy(sb, bp->data, 512);
    brelse(bp);

    bp = bread(dev, sb_blockno + 1);
    if (!bp || !bp->data) {
        panic("ext2fs_readsb: failed to read superblock (second half)");
    }
    memcpy((uint8_t *)sb + 512, bp->data, 512);
    brelse(bp);

    boot_message(INFO,
                 "EXT2: Magic: %x, Inode Size: %d, Block Size: %d",
                 sb->s_magic,
                 sb->s_inode_size,
                 1024 << sb->s_log_block_size);
}

struct icache icache;

static inline void __attribute__((unused)) ext2_inode_pin(struct ext2_inode *ip)
{
    spinlock_acquire(&icache.lock);
    ip->ref++;
    spinlock_release(&icache.lock);
}

static inline void ext2_inode_unpin(struct ext2_inode *ip)
{
    ext2fs_iput(ip);
}

static struct ext2_inode *iget(uint32_t dev, uint32_t inum)
{
    struct ext2_inode *ip;

    // Reject out-of-range inode numbers (e.g. from a corrupt directory entry)
    // before they drive group-descriptor and inode-table offset math.
    if (inum == 0 || inum > ext2_get_sb(dev)->s_inodes_count)
        return nullptr;

    spinlock_acquire(&icache.lock);

    // Is the inode already cached?
    struct ext2_inode *empty = nullptr;
    for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            spinlock_release(&icache.lock);
            return ip;
        }
        if (empty == nullptr && ip->ref == 0) // Remember empty slot.
            empty = ip;
    }

    // Recycle an inode cache entry.
    if (empty == nullptr) {
        panic("iget: no inodes");
    }

    ip        = empty;
    ip->dev   = dev;
    ip->inum  = inum;
    ip->ref   = 1;
    ip->valid = 0;
    ip->type  = 0;
    ip->i_mode = 0;
    ip->size  = 0;
    ip->nlink = 0;
    ip->addrs = &ext2fs_addrs[ip - icache.inode];
    memset(ip->addrs, 0, sizeof(struct ext2fs_addrs));
    spinlock_release(&icache.lock);

    return ip;
}

// Zero a block.
static void ext2fs_bzero(uint32_t dev, uint32_t bno)
{
    buffer_head_t *bp = bread(dev, bno);
    if (!bp) {
        printk("ext2fs_bzero: bread failed\n");
        return;
    }
    memset(bp->data, 0, 512);
    bwrite(bp);
    brelse(bp);
}

// Recursively free an indirect block tree.
// depth=1: single-indirect, depth=2: double-indirect, depth=3: triple-indirect
static void ext2_free_indirect(uint32_t dev, uint32_t block, int depth)
{
    if (block == 0 || depth <= 0)
        return;

    buffer_head_t *bp = bread(dev, BLOCK_TO_SECTOR(block) + ext2_part_offset(dev));
    if (!bp) {
        printk("ext2_free_indirect: bread failed\n");
        return;
    }
    uint32_t *ptrs = (uint32_t *)bp->data;

    for (uint32_t i = 0; i < EXT2_INDIRECT; i++) {
        if (ptrs[i] == 0)
            continue;
        if (depth > 1)
            ext2_free_indirect(dev, ptrs[i], depth - 1);
        else
            ext2fs_bfree(dev, ptrs[i]);
        ptrs[i] = 0;
    }

    brelse(bp);
    ext2fs_bfree(dev, block);
}

// check if a block is free and return its bit number
// Helper: find and set a free bit in bitmap sector
// Returns bit index (absolute) or -1 if none found
static int find_and_set_free_bit(uint8_t *sector_data, uint32_t start_bit, uint32_t max_bits)
{
    for (uint32_t i = 0; i < 512; i++) {
        for (uint32_t j = 0; j < 8; j++) {
            const uint32_t bit = start_bit + i * 8 + j;
            if (bit >= max_bits)
                return -1;
            const uint8_t mask = (uint8_t)(1U << j);
            if ((sector_data[i] & mask) != 0)
                continue;
            sector_data[i] |= mask;
            return (int)bit;
        }
    }
    return -1;
}

static int ext2_claim_free_bitmap_bit(uint32_t dev, uint32_t bitmap_block, uint32_t max_bits, bool fail_on_read,
                                      const char *read_error, buffer_head_t **bp_out)
{
    constexpr uint32_t sectors_per_block = EXT2_BSIZE / 512;
    const uint32_t first_sector          = BLOCK_TO_SECTOR(bitmap_block) + ext2_part_offset(dev);

    *bp_out = nullptr;
    for (uint32_t sec = 0; sec < sectors_per_block; sec++) {
        buffer_head_t *bp = bread(dev, first_sector + sec);
        if (!bp) {
            if (!fail_on_read)
                continue;
            if (read_error)
                printk("%s\n", read_error);
            return -2;
        }

        const uint32_t start_bit = sec * 512 * 8;
        int fbit                 = find_and_set_free_bit(bp->data, start_bit, max_bits);
        if (fbit >= 0) {
            *bp_out = bp;
            return fbit;
        }
        brelse(bp);
    }

    return -1;
}

static int ext2_open_bitmap_bit_sector(uint32_t dev, uint32_t bitmap_block, uint32_t bit_index, buffer_head_t **bp_out,
                                       uint32_t *byte_index_out, u8 *mask_out)
{
    constexpr uint32_t bits_per_sector = 512 * 8;
    const uint32_t sector_in_block     = bit_index / bits_per_sector;
    const uint32_t bit_in_sector       = bit_index % bits_per_sector;

    *byte_index_out = bit_in_sector / 8;
    *mask_out       = (u8)(1U << (bit_in_sector % 8));
    *bp_out         = bread(dev, BLOCK_TO_SECTOR(bitmap_block) + ext2_part_offset(dev) + sector_in_block);
    return (*bp_out != nullptr) ? 0 : -1;
}

static inline void ext2_read_group_desc(uint32_t dev, uint32_t gno, struct ext2_group_desc *out)
{
    const uint32_t desc_blockno = ext2_part_offset(dev) + BLOCK_TO_SECTOR(2);
    buffer_head_t *bp           = bread(dev, desc_blockno);
    if (!bp) {
        printk("ext2_read_group_desc: bread failed\n");
        memset(out, 0, sizeof(*out));
        return;
    }
    memcpy(out, bp->data + gno * sizeof(*out), sizeof(*out));
    brelse(bp);
}

// Read a pointer entry from a block and allocate it if empty.
static uint32_t ext2_ensure_ptr(uint32_t dev, uint32_t block, uint32_t slot, uint32_t inum)
{
    const uint32_t sector = BLOCK_TO_SECTOR(block) + ext2_part_offset(dev) + (slot / PTRS_PER_SECTOR);
    buffer_head_t *bp     = bread(dev, sector);
    if (!bp) {
        printk("ext2_ensure_ptr: bread failed\n");
        return 0;
    }
    uint32_t *table = (uint32_t *)bp->data;
    uint32_t *entry = &table[slot % PTRS_PER_SECTOR];
    uint32_t val    = *entry;
    if (val == 0) {
        val = ext2fs_balloc(dev, inum);
        if (val == 0) {
            brelse(bp);
            return 0;
        }
        *entry = val;
        bwrite(bp);
    }
    brelse(bp);
    return val;
}

// Allocate a zeroed disk block.
static uint32_t ext2fs_balloc(uint32_t dev, uint32_t inum)
{
    struct ext2_super_block *sb          = ext2_get_sb(dev);
    const uint32_t bgcount               = (sb->s_blocks_count + sb->s_blocks_per_group - 1) / sb->s_blocks_per_group;
    const int preferred_gno              = GET_GROUP_NO(inum, *sb);
    constexpr uint32_t sectors_per_block = EXT2_BSIZE / 512;

    // Search all block groups, starting with the inode's group
    for (uint32_t gi = 0; gi < bgcount; gi++) {
        const uint32_t gno = (preferred_gno + gi) % bgcount;
        struct ext2_group_desc bgdesc;
        ext2_read_group_desc(dev, gno, &bgdesc);

        buffer_head_t *bp = nullptr;
        int fbit = ext2_claim_free_bitmap_bit(dev, bgdesc.bg_block_bitmap, sb->s_blocks_per_group, false, nullptr, &bp);
        if (fbit < 0)
            continue;

        bwrite(bp);
        brelse(bp);

        const uint32_t group_first_block = sb->s_first_data_block + gno * sb->s_blocks_per_group;
        const uint32_t rel_block         = group_first_block + (uint32_t)fbit;

        const uint32_t start_sector = BLOCK_TO_SECTOR(rel_block) + ext2_part_offset(dev);
        for (uint32_t i = 0; i < sectors_per_block; i++) {
            ext2fs_bzero(dev, start_sector + i);
        }
        return rel_block;
    }

    printk("PANIC: ");
    printk("ext2_balloc: out of blocks\n");
    return 0;
}

// Free a disk block.
static void ext2fs_bfree(uint32_t dev, uint32_t b)
{
    struct ext2_super_block *sb = ext2_get_sb(dev);
    if (b < sb->s_first_data_block) {
        printk("PANIC: ");
        printk("ext2fs_bfree: invalid block\n");
        return;
    }

    const uint32_t block_index = b - sb->s_first_data_block;
    const uint32_t gno         = block_index / sb->s_blocks_per_group;
    const uint32_t offset      = block_index % sb->s_blocks_per_group;

    struct ext2_group_desc bgdesc;
    ext2_read_group_desc(dev, gno, &bgdesc);

    buffer_head_t *bp = nullptr;
    uint32_t byte_index;
    u8 mask;
    if (ext2_open_bitmap_bit_sector(dev, bgdesc.bg_block_bitmap, offset, &bp, &byte_index, &mask) != 0) {
        printk("ext2fs_bfree: bread failed\n");
        return;
    }

    if ((bp->data[byte_index] & mask) == 0) {
        printk("PANIC: ");
        printk("ext2fs_bfree: block already free\n");
        brelse(bp);
        return;
    }
    bp->data[byte_index] &= ~mask;
    bwrite(bp);
    brelse(bp);
}

void ext2_init_inode(int dev)
{
    // mbr_load();
    struct ext2_super_block *sb = ext2_get_sb(dev);
    ext2fs_readsb(dev, sb);
    const uint32_t block_bytes = 1024u << sb->s_log_block_size;
    const u64 partition_mb     = ((u64)sb->s_blocks_count * block_bytes) / (1024ull * 1024ull);
    const u64 size_value       = (partition_mb >= 1024ull) ? partition_mb / 1024ull : partition_mb;
    const char *size_suffix    = (partition_mb >= 1024ull) ? "GB" : "MB";
    printk("ext2: size: %llu %s, block_size: %u, block_count: %u, inodes: %u",
           (unsigned long long)size_value,
           size_suffix,
           block_bytes,
           sb->s_blocks_count,
           sb->s_inodes_count);
}

// Helper to compute sector and byte offset for an inode within its group's inode table.
// Returns the absolute sector number and the byte offset within that sector.
static void ext2_inode_loc(uint32_t dev, uint32_t inum, uint32_t *sector, uint32_t *byte_offset)
{
    struct ext2_super_block *sb = ext2_get_sb(dev);
    const int gno               = GET_GROUP_NO(inum, *sb);
    const int ioff              = GET_INODE_INDEX(inum, *sb);
    struct ext2_group_desc bgdesc;
    ext2_read_group_desc(dev, gno, &bgdesc);

    const uint32_t inodes_per_block = EXT2_BSIZE / sb->s_inode_size;
    const uint32_t bno    = BLOCK_TO_SECTOR(bgdesc.bg_inode_table + ioff / inodes_per_block) + ext2_part_offset(dev);
    const uint32_t iindex = ioff % inodes_per_block;

    const uint32_t block_off = iindex * sb->s_inode_size;
    *sector                  = bno + block_off / 512;
    *byte_offset             = block_off % 512;
}

static int ext2_open_inode_slot(uint32_t dev, uint32_t inum, buffer_head_t **bp_out, u8 **slot_out)
{
    uint32_t sector;
    uint32_t byte_offset;
    ext2_inode_loc(dev, inum, &sector, &byte_offset);

    buffer_head_t *bp = bread(dev, sector);
    if (!bp)
        return -1;

    *bp_out   = bp;
    *slot_out = bp->data + byte_offset;
    return 0;
}

struct ext2_inode *ext2fs_ialloc(uint32_t dev, short type)
{
    struct ext2_super_block *sb = ext2_get_sb(dev);
    struct ext2_group_desc bgdesc;
    const uint32_t desc_blockno = ext2_part_offset(dev) + BLOCK_TO_SECTOR(2);
    // block group descriptor table starts at block 2

    const uint32_t bgcount = sb->s_blocks_count / sb->s_blocks_per_group;

    buffer_head_t *group_desc_buf = bread(dev, desc_blockno);
    if (!group_desc_buf) {
        printk("ext2fs_ialloc: bread failed for group desc\n");
        return nullptr;
    }

    for (uint32_t i = 0; i <= bgcount; i++) {
        memcpy(&bgdesc, group_desc_buf->data + i * sizeof(bgdesc), sizeof(bgdesc));

        buffer_head_t *ibitmap_buff = nullptr;
        int fbit = ext2_claim_free_bitmap_bit(dev,
                                              bgdesc.bg_inode_bitmap,
                                              sb->s_inodes_per_group,
                                              true,
                                              "ext2fs_ialloc: bread failed for inode bitmap",
                                              &ibitmap_buff);
        if (fbit == -2) {
            brelse(group_desc_buf);
            return nullptr;
        }
        if (fbit < 0) {
            continue;
        }

        if (sb->s_inode_size == 0) {
            printk("PANIC: ");
            printk("ext2fs_ialloc: invalid inode size");
            brelse(ibitmap_buff);
            brelse(group_desc_buf);
            return nullptr;
        }

        const uint32_t inodes_per_block = EXT2_BSIZE / sb->s_inode_size;
        const uint32_t inode_block =
            BLOCK_TO_SECTOR(bgdesc.bg_inode_table + (uint32_t)fbit / inodes_per_block) + ext2_part_offset(dev);
        const uint32_t iindex = (uint32_t)fbit % inodes_per_block;

        const uint32_t block_offset  = iindex * sb->s_inode_size;
        const uint32_t sector_offset = block_offset / 512;

        buffer_head_t *dinode_buff = bread(dev, inode_block + sector_offset);
        if (!dinode_buff) {
            printk("ext2fs_ialloc: bread failed for inode block\n");
            brelse(ibitmap_buff);
            brelse(group_desc_buf);
            return nullptr;
        }
        u8 *slot = dinode_buff->data + (block_offset % 512);

        memset(slot, 0, sb->s_inode_size);
        struct ext2_disk_inode *din = (struct ext2_disk_inode *)slot;
        uint16_t mode = 0;
        if (type == T_DIR) {
            mode = (uint16_t)(S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        } else if (type == T_FILE) {
            mode = (uint16_t)(S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        } else if (type == T_DEV) {
            mode = (uint16_t)(S_IFCHR | S_IRUSR | S_IWUSR);
        }
        din->i_mode = mode;
        bwrite(dinode_buff);
        bwrite(ibitmap_buff);
        brelse(dinode_buff);
        brelse(ibitmap_buff);

        const uint32_t inum = i * sb->s_inodes_per_group + (uint32_t)fbit + 1;
        struct ext2_inode *ip = iget(dev, inum);
        ip->type              = type;
        ip->i_mode            = mode;
        brelse(group_desc_buf);
        return ip;
    }
    brelse(group_desc_buf);
    printk("PANIC: ");
    printk("ext2_ialloc: no inodes");
    return nullptr;
}

void ext2fs_iupdate(const struct ext2_inode *ip)
{
    struct ext2_super_block *sb = ext2_get_sb(ip->dev);
    if (sb->s_inode_size > EXT2_MAX_INODE_SIZE) {
        printk("PANIC: ");
        printk("ext2fs_iupdate: inode too large");
        return;
    }

    buffer_head_t *bp1 = nullptr;
    u8 *slot           = nullptr;
    if (ext2_open_inode_slot(ip->dev, ip->inum, &bp1, &slot) != 0) {
        printk("ext2fs_iupdate: bread failed\n");
        return;
    }

    struct ext2_disk_inode *din = (struct ext2_disk_inode *)slot;

    uint16_t mode = ip->i_mode;
    if ((mode & S_IFMT) == 0)
        mode = (uint16_t)vfs_mode_from_type(ip->type);
    din->i_mode = mode;

    din->i_atime = ip->i_atime;
    din->i_ctime = ip->i_ctime;
    din->i_mtime = ip->i_mtime;
    din->i_dtime = din->i_uid = din->i_gid = din->i_flags = din->i_generation = 0;
    din->i_links_count                                                        = ip->nlink;
    din->i_size                                                               = ip->size;

    const struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;
    memcpy(din->i_block, ad->addrs, sizeof(ad->addrs));

    if (ip->type == T_DEV) {
        din->i_block[0] = (ip->major << 8) | ip->minor;
    }

    // memcpy(bp1->data + sector_byte_offset, raw, ext2_sb.s_inode_size);
    bwrite(bp1);
    brelse(bp1);
}

int ext2fs_ilock(struct ext2_inode *ip)
{
    if (ip == nullptr || ip->ref < 1) {
        return -1;
    }

    assert(ip->addrs != nullptr, "ip->addrs is null in ext2fs_ilock before lock");
    sleeplock_acquire(&ip->lock);
    if (ip->addrs == nullptr) {
        sleeplock_release(&ip->lock);
        return -1;
    }
    auto const ad = (struct ext2fs_addrs *)ip->addrs;

    if (ip->valid == 0) {
        struct ext2_super_block *sb = ext2_get_sb(ip->dev);
        if (sb->s_inode_size > EXT2_MAX_INODE_SIZE) {
            sleeplock_release(&ip->lock);
            return -1;
        }

        buffer_head_t *bp1 = nullptr;
        u8 *slot           = nullptr;
        if (ext2_open_inode_slot(ip->dev, ip->inum, &bp1, &slot) != 0) {
            sleeplock_release(&ip->lock);
            return -1;
        }

        u8 raw[EXT2_MAX_INODE_SIZE];
        memcpy(raw, slot, sb->s_inode_size);
        brelse(bp1);

        const struct ext2_disk_inode *din = (struct ext2_disk_inode *)raw;
        ip->i_mode                        = din->i_mode;

        if (S_ISDIR(din->i_mode) || din->i_mode == T_DIR) {
            ip->type = T_DIR;
        } else if (S_ISREG(din->i_mode)) {
            ip->type = T_FILE;
        } else if (S_ISCHR(din->i_mode)) {
            ip->type  = T_DEV;
            ip->major = (din->i_block[0] >> 8) & 0xFF;
            ip->minor = din->i_block[0] & 0xFF;
        }
        ip->i_atime = din->i_atime;
        ip->i_ctime = din->i_ctime;
        ip->i_mtime = din->i_mtime;
        ip->i_dtime = din->i_dtime;
        ip->i_uid   = din->i_uid;
        ip->i_gid   = din->i_gid;
        ip->i_flags = din->i_flags;

        ip->nlink = din->i_links_count;
        ip->size  = din->i_size;
        memcpy(ad->addrs, din->i_block, sizeof(ad->addrs));

        ip->valid = 1;
        if (ip->type == 0) {
            sleeplock_release(&ip->lock);
            return -1;
        }
    }

    if (ip->type == 0) {
        sleeplock_release(&ip->lock);
        return -1;
    }
    return 0;
}

void ext2fs_iunlock(struct ext2_inode *ip)
{
    if (ip == nullptr || !sleeplock_holding(&ip->lock) || ip->ref < 1)
        panic("ext2fs_iunlock: invalid inode");

    sleeplock_release(&ip->lock);
}

// Free an inode
static void ext2_free_inode(const struct ext2_inode *ip)
{
    struct ext2_super_block *sb = ext2_get_sb(ip->dev);
    struct ext2_group_desc bgdesc;
    const int gno = GET_GROUP_NO(ip->inum, *sb);
    ext2_read_group_desc(ip->dev, gno, &bgdesc);

    const uint32_t index = (ip->inum - 1) % sb->s_inodes_per_group;

    buffer_head_t *bp2 = nullptr;
    uint32_t byte_index;
    u8 mask;
    if (ext2_open_bitmap_bit_sector(ip->dev, bgdesc.bg_inode_bitmap, index, &bp2, &byte_index, &mask) != 0) {
        printk("ext2_free_inode: bread failed\n");
        return;
    }

    if ((bp2->data[byte_index] & mask) == 0) {
        printk("PANIC: ");
        printk("ext2fs_ifree: inode already free (inum=%u type=%d nlink=%d ref=%d)\n",
               ip->inum,
               ip->type,
               ip->nlink,
               ip->ref);
    }
    bp2->data[byte_index] &= ~mask;
    bwrite(bp2);
    brelse(bp2);
}

void ext2fs_iput(struct ext2_inode *ip)
{
    sleeplock_acquire(&ip->lock);
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    if (ip->valid && ip->nlink == 0) {
        spinlock_acquire(&icache.lock);
        const int r = ip->ref;
        spinlock_release(&icache.lock);
        if (r == 1) {
            // inode has no links and no other references: truncate and free.
            ext2_free_inode(ip);
            ext2fs_itrunc(ip);
            ip->type = 0;
            ext2fs_iupdate(ip);
            ip->valid = 0;
        }
    }
    sleeplock_release(&ip->lock);

    spinlock_acquire(&icache.lock);
    ip->ref--;
    if (ip->ref == 0) {
        if (ad)
            ad->busy = 0;
        ip->addrs = nullptr;
    }
    spinlock_release(&icache.lock);
}

void ext2fs_iunlockput(struct ext2_inode *ip)
{
    ext2fs_iunlock(ip);
    ext2fs_iput(ip);
}

void ext2_stat_inode(const struct ext2_inode *ip, struct stat *st)
{
    st->st_dev   = clamp_to_int(ip->dev);
    st->st_ino   = clamp_to_int(ip->inum);
    st->st_mode  = ((ip->i_mode & S_IFMT) != 0) ? ip->i_mode : vfs_mode_from_type(ip->type);
    st->st_nlink = ip->nlink;
    st->st_size  = ip->size;
    st->st_atime = ip->i_atime;
    st->st_ctime = ip->i_ctime;
    st->st_mtime = ip->i_mtime;
    st->st_uid   = ip->i_uid;
    st->st_gid   = ip->i_gid;
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one:
/*
 * EXT2EXT2_BSIZE -> 1024
 * If < EXT2_NDIR_BLOCKS then it is directly mapped, allocate and return
 * If < 128 (Indirect blocks) then need to allocate using indirect block
 * If < 128*128 (Double indirect) ...
 * If < 128*128*128 (Triple indirect) ...
 * Else panic()
 */
static uint32_t ext2fs_bmap(const struct ext2_inode *ip, uint32_t bn)
{
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    if (bn < EXT2_NDIR_BLOCKS) {
        if (ad->addrs[bn] == 0) {
            ad->addrs[bn] = ext2fs_balloc(ip->dev, ip->inum);
            if (ad->addrs[bn] == 0)
                return 0;
        }
        return BLOCK_TO_SECTOR(ad->addrs[bn]) + ext2_part_offset(ip->dev);
    }
    bn -= EXT2_NDIR_BLOCKS;
    if (bn < EXT2_INDIRECT) {
        if (ad->addrs[EXT2_IND_BLOCK] == 0) {
            ad->addrs[EXT2_IND_BLOCK] = ext2fs_balloc(ip->dev, ip->inum);
            if (ad->addrs[EXT2_IND_BLOCK] == 0)
                return 0;
        }
        const uint32_t entry = ext2_ensure_ptr(ip->dev, ad->addrs[EXT2_IND_BLOCK], bn, ip->inum);
        if (entry == 0)
            return 0;
        return BLOCK_TO_SECTOR(entry) + ext2_part_offset(ip->dev);
    }
    bn -= EXT2_INDIRECT;

    if (bn < EXT2_DINDIRECT) {
        if (ad->addrs[EXT2_DIND_BLOCK] == 0) {
            ad->addrs[EXT2_DIND_BLOCK] = ext2fs_balloc(ip->dev, ip->inum);
            if (ad->addrs[EXT2_DIND_BLOCK] == 0)
                return 0;
        }

        const uint32_t first_index  = bn / EXT2_INDIRECT;
        const uint32_t second_index = bn % EXT2_INDIRECT;

        const uint32_t mid = ext2_ensure_ptr(ip->dev, ad->addrs[EXT2_DIND_BLOCK], first_index, ip->inum);
        if (mid == 0)
            return 0;
        const uint32_t leaf = ext2_ensure_ptr(ip->dev, mid, second_index, ip->inum);
        if (leaf == 0)
            return 0;
        return BLOCK_TO_SECTOR(leaf) + ext2_part_offset(ip->dev);
    }
    bn -= EXT2_DINDIRECT;

    if (bn < EXT2_TINDIRECT) {
        if (ad->addrs[EXT2_TIND_BLOCK] == 0) {
            ad->addrs[EXT2_TIND_BLOCK] = ext2fs_balloc(ip->dev, ip->inum);
            if (ad->addrs[EXT2_TIND_BLOCK] == 0)
                return 0;
        }

        const uint32_t first_index  = bn / EXT2_DINDIRECT;
        const uint32_t remainder    = bn % EXT2_DINDIRECT;
        const uint32_t second_index = remainder / EXT2_INDIRECT;
        const uint32_t third_index  = remainder % EXT2_INDIRECT;

        const uint32_t level1 = ext2_ensure_ptr(ip->dev, ad->addrs[EXT2_TIND_BLOCK], first_index, ip->inum);
        if (level1 == 0)
            return 0;
        const uint32_t level2 = ext2_ensure_ptr(ip->dev, level1, second_index, ip->inum);
        if (level2 == 0)
            return 0;
        const uint32_t leaf = ext2_ensure_ptr(ip->dev, level2, third_index, ip->inum);
        if (leaf == 0)
            return 0;
        return BLOCK_TO_SECTOR(leaf) + ext2_part_offset(ip->dev);
    }
    printk("PANIC: ");
    printk("ext2_bmap: block number out of range\n");
    return 0;
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void ext2fs_itrunc(struct ext2_inode *ip)
{
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (ad->addrs[i]) {
            ext2fs_bfree(ip->dev, ad->addrs[i]);
            ad->addrs[i] = 0;
        }
    }

    if (ad->addrs[EXT2_IND_BLOCK]) {
        ext2_free_indirect(ip->dev, ad->addrs[EXT2_IND_BLOCK], 1);
        ad->addrs[EXT2_IND_BLOCK] = 0;
    }

    if (ad->addrs[EXT2_DIND_BLOCK]) {
        ext2_free_indirect(ip->dev, ad->addrs[EXT2_DIND_BLOCK], 2);
        ad->addrs[EXT2_DIND_BLOCK] = 0;
    }

    if (ad->addrs[EXT2_TIND_BLOCK]) {
        ext2_free_indirect(ip->dev, ad->addrs[EXT2_TIND_BLOCK], 3);
        ad->addrs[EXT2_TIND_BLOCK] = 0;
    }

    ip->size = 0;
    ext2fs_iupdate(ip);
}

int ext2_read_inode(const struct ext2_inode *ip, char *dst, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV) {
        return -1;
    }

    if (off >= ip->size || off + n < off) {
        return 0;
    }
    if (off + n > ip->size) {
        n = ip->size - off;
    }

    for (uint32_t tot = 0; tot < n;) {
        const uint32_t logical_block = off / EXT2_BSIZE;
        const uint32_t sector_start  = ext2fs_bmap(ip, logical_block); // Returns starting sector
        if (sector_start == 0) {
            return -1;
        }
        const uint32_t offset_in_block = off % EXT2_BSIZE;

        const uint32_t sector_offset = offset_in_block / 512;
        const uint32_t sector        = sector_start + sector_offset;


        buffer_head_t *bp = bread(ip->dev, sector);
        if (!bp) {
            return -1;
        }


        const uint32_t offset_in_sector = offset_in_block % 512;
        const uint32_t bytes_to_copy    = min(n - tot, 512 - offset_in_sector);

        memcpy(dst, bp->data + offset_in_sector, bytes_to_copy);
        brelse(bp);

        tot += bytes_to_copy;
        off += bytes_to_copy;
        dst += bytes_to_copy;
    }

    return clamp_to_int(n);
}

int ext2_write_inode(struct ext2_inode *ip, const char *src, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV) {
        return -1;
    }

    if (off > ip->size || off + n < off) {
        return -1;
    }
    if ((uint64_t)off + n > (uint64_t)EXT2_MAXFILE * EXT2_BSIZE) {
        return -1;
    }

    for (uint32_t tot = 0; tot < n;) {
        const uint32_t logical_block = off / EXT2_BSIZE;
        const uint32_t sector_start  = ext2fs_bmap(ip, logical_block); // Returns starting sector
        if (sector_start == 0) {
            return -1;
        }
        const uint32_t offset_in_block = off % EXT2_BSIZE;

        const uint32_t sector_offset = offset_in_block / 512;
        const uint32_t sector        = sector_start + sector_offset;

        buffer_head_t *bp = bread(ip->dev, sector);
        if (!bp) {
            return -1;
        }

        const uint32_t offset_in_sector = offset_in_block % 512;
        const uint32_t bytes_to_copy    = min(n - tot, 512 - offset_in_sector);

        memcpy(bp->data + offset_in_sector, src, bytes_to_copy);
        bwrite(bp);
        brelse(bp);

        tot += bytes_to_copy;
        off += bytes_to_copy;
        src += bytes_to_copy;
    }

    if (n > 0) {
        if (off > ip->size)
            ip->size = off;
        ext2fs_iupdate(ip);
    }
    return clamp_to_int(n);
}

int ext2fs_namecmp(const char *s, const char *t)
{
    return strncmp(s, t, EXT2_NAME_LEN);
}

struct ext2_inode *ext2fs_dirlookup(const struct ext2_inode *dp, const char *name, uint32_t *poff)
{
    struct ext2_dir_entry_2 de;
    char file_name[EXT2_NAME_LEN + 1];


    for (uint32_t off = 0; off + 8 <= dp->size;) {
        if (ext2_read_inode(dp, (char *)&de, off, 8) != 8)
            break;
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE || off + de.rec_len > dp->size) {
            panic("ext2fs_dirlookup: bad rec_len");
        }


        if (de.inode == 0) {
            off += de.rec_len;
            continue;
        }

        int to_copy = de.name_len;
        if (to_copy > EXT2_NAME_LEN)
            to_copy = EXT2_NAME_LEN;
        if (to_copy > 0 && ext2_read_inode(dp, (char *)de.name, off + 8, to_copy) != to_copy) {
            panic("ext2fs_dirlookup: name read");
        }
        memcpy(file_name, de.name, to_copy);
        file_name[to_copy] = '\0';


        if (ext2fs_namecmp(name, file_name) == 0) {
            if (poff) {
                *poff = off;
            }
            return iget(dp->dev, de.inode);
        }
        off += de.rec_len;
    }
    return nullptr;
}

int ext2fs_dirlink(struct ext2_inode *dp, const char *name, uint32_t inum)
{
    if (name == nullptr) {
        return -1;
    }

    const size_t name_len = strlen(name);
    if (name_len == 0 || name_len > EXT2_NAME_LEN) {
        return -1;
    }

    struct ext2_dir_entry_2 de;
    struct ext2_inode *ip;

    if ((ip = ext2fs_dirlookup(dp, name, nullptr)) != nullptr) {
        ext2fs_iput(ip);
        return -1;
    }

    const uint16_t needed = ext2_dirent_size((u8)name_len);

    // Search for free space in existing directory entries
    for (uint32_t off = 0; off + 8 <= dp->size;) {
        if (ext2_read_inode(dp, (char *)&de, off, 8) != 8)
            break;
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE)
            break;

        uint16_t actual     = (de.inode == 0) ? 0 : ext2_dirent_size(de.name_len);
        uint16_t free_space = de.rec_len - actual;

        if (free_space >= needed) {
            // Found space - split this entry
            if (de.inode != 0) {
                // Shrink existing entry to its actual size
                uint16_t old_rec_len = de.rec_len;
                de.rec_len           = actual;
                if (ext2_write_inode(dp, (char *)&de, off, 8) != 8)
                    return -1;

                // Write new entry after the existing one
                off += actual;
                de.inode     = inum;
                de.rec_len   = old_rec_len - actual;
                de.name_len  = (uint8_t)name_len;
                de.file_type = EXT2_FT_UNKNOWN;
                memcpy(de.name, name, name_len);

                if (ext2_write_inode(dp, (char *)&de, off, 8 + name_len) != (int)(8 + name_len))
                    return -1;
            } else {
                // Reuse deleted entry
                de.inode = inum;
                // Keep rec_len as is - it includes slack space
                de.name_len  = (uint8_t)name_len;
                de.file_type = EXT2_FT_UNKNOWN;
                memcpy(de.name, name, name_len);

                if (ext2_write_inode(dp, (char *)&de, off, 8 + name_len) != (int)(8 + name_len))
                    return -1;
            }
            return 0;
        }

        off += de.rec_len;
    }

    // No space found - need to extend directory to a new block
    // Calculate position for new entry (should be at block boundary or end of dir)
    uint32_t off = dp->size;

    // If we're not at a block boundary, we need to extend the last entry's rec_len
    // to fill the current block, then add new entry in new block
    uint32_t block_off = off % EXT2_BSIZE;
    if (block_off != 0) {
        // Find and extend the last entry to fill the rest of this block
        uint32_t last_off = 0;
        for (uint32_t scan = 0; scan + 8 <= dp->size;) {
            if (ext2_read_inode(dp, (char *)&de, scan, 8) != 8)
                break;
            if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE)
                break;
            last_off = scan;
            scan += de.rec_len;
        }

        // Read the last entry and extend its rec_len
        if (ext2_read_inode(dp, (char *)&de, last_off, 8) == 8) {
            uint16_t new_rec_len = de.rec_len + (EXT2_BSIZE - block_off);
            de.rec_len           = new_rec_len;
            ext2_write_inode(dp, (char *)&de, last_off, 8);
        }

        // Extend directory size to block boundary
        dp->size = ((dp->size + EXT2_BSIZE - 1) / EXT2_BSIZE) * EXT2_BSIZE;
        off      = dp->size;
    }

    // Now add new entry at the start of a new block
    de.inode     = inum;
    de.rec_len   = EXT2_BSIZE; // Takes the whole block (last entry in block)
    de.name_len  = (uint8_t)name_len;
    de.file_type = EXT2_FT_UNKNOWN;
    memcpy(de.name, name, name_len);

    if (ext2_write_inode(dp, (char *)&de, off, 8 + name_len) != (int)(8 + name_len)) {
        printk("ext2fs_dirlink: writei failed\n");
        return -1;
    }

    // Update directory size
    dp->size = off + EXT2_BSIZE;
    ext2fs_iupdate(dp);

    return 0;
}

// VFS Wrappers

static uint64_t ext2_vfs_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (!node || !node->device || !buffer || size == 0)
        return 0;
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (ip == nullptr)
        return 0;

    if (ext2fs_ilock(ip) != 0)
        return 0;

    const int n = ext2_read_inode(ip, (char *)buffer, offset, size);

    ext2fs_iunlock(ip);

    return n > 0 ? n : 0;
}

static uint64_t ext2_vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    if (!node || !node->device || !buffer || size == 0)
        return 0;
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (!ip)
        return 0;

    ext2_lock(ip->dev);
    if (ext2fs_ilock(ip) != 0) {
        ext2_unlock(ip->dev);
        return 0;
    }
    const int n = ext2_write_inode(ip, (char *)buffer, offset, size);
    if (n > 0)
        node->size = ip->size;
    ext2fs_iunlock(ip);
    ext2_unlock(ip->dev);
    return n > 0 ? n : 0;
}

static int ext2_vfs_truncate(vfs_inode_t *node)
{
    if (!node || !node->device)
        return -1;
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (!ip)
        return -1;

    ext2_lock(ip->dev);
    if (ext2fs_ilock(ip) != 0) {
        ext2_unlock(ip->dev);
        return -1;
    }
    ext2fs_itrunc(ip);
    ext2fs_iunlock(ip);
    ext2_unlock(ip->dev);
    node->size = 0;
    return 0;
}

static void ext2_vfs_open(const vfs_inode_t *node)
{
    (void)node;
    // Nothing to do
}

static void ext2_vfs_close(vfs_inode_t *node)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (ip) {
        ext2_inode_unpin(ip);
        node->device = nullptr;
    }
}

static struct inode_operations ext2_vfs_ops;

static int ext2_vfs_link(vfs_inode_t *parent, const char *name, vfs_inode_t *target)
{
    if (!parent || !target || !name)
        return -EINVAL;
    if ((parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY)
        return -ENOTDIR;

    struct ext2_inode *dp = (struct ext2_inode *)parent->device;
    struct ext2_inode *ip = (struct ext2_inode *)target->device;
    if (!dp || !ip)
        return -EIO;
    if (dp->dev != ip->dev)
        return -ENOTSUP;
    if (ip->type == T_DIR)
        return -EPERM;

    ext2_lock(dp->dev);
    if (ext2fs_ilock(dp) != 0) {
        ext2_unlock(dp->dev);
        return -EIO;
    }

    struct ext2_inode *existing = ext2fs_dirlookup(dp, name, nullptr);
    if (existing) {
        ext2fs_iput(existing);
        ext2fs_iunlock(dp);
        ext2_unlock(dp->dev);
        return -EINSTKN;
    }

    int res = ext2fs_dirlink(dp, name, ip->inum);
    ext2fs_iunlock(dp);
    if (res < 0) {
        ext2_unlock(dp->dev);
        return -EIO;
    }

    if (ext2fs_ilock(ip) != 0) {
        ext2_unlock(dp->dev);
        return -EIO;
    }
    ip->nlink++;
    ext2fs_iupdate(ip);
    ext2fs_iunlock(ip);
    ext2_unlock(dp->dev);
    return ALL_OK;
}

static int ext2_vfs_unlink(vfs_inode_t *parent, const char *name)
{
    if (!parent || !name)
        return -EINVAL;
    if ((parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY)
        return -ENOTDIR;

    struct ext2_inode *dp = (struct ext2_inode *)parent->device;
    if (!dp)
        return -EIO;

    ext2_lock(dp->dev);
    if (ext2fs_ilock(dp) != 0) {
        ext2_unlock(dp->dev);
        return -EIO;
    }
    uint32_t off          = 0;
    struct ext2_inode *ip = ext2fs_dirlookup(dp, name, &off);
    if (!ip) {
        ext2fs_iunlock(dp);
        ext2_unlock(dp->dev);
        return -ENOENT;
    }

    // Do not allow unlinking directories.
    if (ip->type == T_DIR) {
        ext2fs_iput(ip);
        ext2fs_iunlock(dp);
        ext2_unlock(dp->dev);
        return -EISDIR;
    }

    // Zero the inode field of the directory entry.
    uint32_t zero = 0;
    if (ext2_write_inode(dp, (char *)&zero, off, sizeof(zero)) != sizeof(zero)) {
        ext2fs_iput(ip);
        ext2fs_iunlock(dp);
        ext2_unlock(dp->dev);
        return -EIO;
    }
    ext2fs_iunlock(dp);

    if (ext2fs_ilock(ip) != 0) {
        ext2fs_iput(ip);
        ext2_unlock(dp->dev);
        return -EIO;
    }
    if (ip->nlink > 0)
        ip->nlink--;
    ext2fs_iupdate(ip);
    ext2fs_iunlock(ip);
    ext2fs_iput(ip);
    ext2_unlock(dp->dev);
    return ALL_OK;
}

static int ext2_direntry_clear(struct ext2_inode *dp, uint32_t off)
{
    uint32_t zero = 0;
    if (ext2_write_inode(dp, (char *)&zero, off, sizeof(zero)) != sizeof(zero))
        return -1;
    return ALL_OK;
}

static int ext2_direntry_update_inode(struct ext2_inode *dp, uint32_t off, uint32_t inum, uint8_t file_type)
{
    struct ext2_dir_entry_2 de;
    if (ext2_read_inode(dp, (char *)&de, off, 8) != 8)
        return -1;
    de.inode     = inum;
    de.file_type = file_type;
    if (ext2_write_inode(dp, (char *)&de, off, 8) != 8)
        return -1;
    return 0;
}

static int ext2_vfs_rename(vfs_inode_t *old_parent, const char *old_name, vfs_inode_t *new_parent, const char *new_name)
{
    if (!old_parent || !new_parent || !old_name || !new_name)
        return -EINVAL;
    if ((old_parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY)
        return -ENOTDIR;
    if ((new_parent->flags & VFS_TYPE_MASK) != VFS_DIRECTORY)
        return -ENOTDIR;

    auto old_dp = (struct ext2_inode *)old_parent->device;
    auto new_dp = (struct ext2_inode *)new_parent->device;
    if (!old_dp || !new_dp)
        return -EIO;
    if (old_dp->dev != new_dp->dev)
        return -ENOTSUP;

    int res                   = -EIO;
    struct ext2_inode *old_ip = nullptr;
    struct ext2_inode *new_ip = nullptr;
    bool old_locked           = false;
    bool new_locked           = false;

    ext2_lock(old_dp->dev);

    if (ext2fs_ilock(old_dp) != 0)
        goto out;
    old_locked = true;
    if (new_dp != old_dp) {
        if (ext2fs_ilock(new_dp) != 0)
            goto out;
        new_locked = true;
    }

    uint32_t old_off = 0;
    old_ip           = ext2fs_dirlookup(old_dp, old_name, &old_off);
    if (!old_ip) {
        res = -ENOENT;
        goto out;
    }
    if (old_ip->type == T_DIR) {
        res = -EPERM;
        goto out;
    }

    if (old_dp == new_dp && strcmp(old_name, new_name) == 0) {
        res = 0;
        goto out;
    }

    uint32_t new_off = 0;
    new_ip           = ext2fs_dirlookup(new_dp, new_name, &new_off);
    if (new_ip) {
        if (new_ip->type == T_DIR) {
            res = -EISDIR;
            goto out;
        }

        if (new_ip->inum == old_ip->inum) {
            if (ext2_direntry_clear(old_dp, old_off) != 0)
                goto out;
            if (ext2fs_ilock(old_ip) != 0)
                goto out;
            if (old_ip->nlink > 0)
                old_ip->nlink--;
            ext2fs_iupdate(old_ip);
            ext2fs_iunlock(old_ip);
            res = 0;
            goto out;
        }

        if (ext2fs_ilock(new_ip) != 0)
            goto out;
        if (ext2fs_ilock(old_ip) != 0) {
            ext2fs_iunlock(new_ip);
            goto out;
        }

        if (ext2_direntry_update_inode(new_dp, new_off, old_ip->inum, (uint8_t)old_ip->type) != 0) {
            ext2fs_iunlock(old_ip);
            ext2fs_iunlock(new_ip);
            goto out;
        }

        if (new_ip->nlink > 0)
            new_ip->nlink--;
        ext2fs_iupdate(new_ip);
        ext2fs_iunlock(new_ip);

        old_ip->nlink++;
        ext2fs_iupdate(old_ip);
        ext2fs_iunlock(old_ip);
    } else {
        if (ext2fs_dirlink(new_dp, new_name, old_ip->inum) < 0)
            goto out;
        if (ext2fs_ilock(old_ip) != 0)
            goto out;
        old_ip->nlink++;
        ext2fs_iupdate(old_ip);
        ext2fs_iunlock(old_ip);
    }

    if (ext2_direntry_clear(old_dp, old_off) != 0)
        goto out;
    if (ext2fs_ilock(old_ip) != 0)
        goto out;
    if (old_ip->nlink > 0)
        old_ip->nlink--;
    ext2fs_iupdate(old_ip);
    ext2fs_iunlock(old_ip);

    res = 0;

out:
    if (new_ip)
        ext2fs_iput(new_ip);
    if (old_ip)
        ext2fs_iput(old_ip);
    if (new_locked)
        ext2fs_iunlock(new_dp);
    if (old_locked)
        ext2fs_iunlock(old_dp);
    ext2_unlock(old_dp->dev);
    return res;
}

static vfs_inode_t *ext2_vfs_finddir(const vfs_inode_t *node, const char *name)
{
    if (!node || !node->device || !name)
        return nullptr;
    auto dp = (struct ext2_inode *)node->device;
    if (!dp)
        return nullptr;

    const uint32_t dev_copy = dp->dev;


    ext2_lock(dev_copy);
    if (ext2fs_ilock(dp) != 0) {
        ext2_unlock(dev_copy);
        return nullptr;
    }
    if (dp->addrs == nullptr) {
        ext2fs_iunlock(dp);
        ext2_unlock(dev_copy);
        return nullptr;
    }

    struct ext2_inode *ip = ext2fs_dirlookup(dp, name, nullptr);
    ext2fs_iunlock(dp);

    if (!ip) {
        ext2_unlock(dev_copy);
        return nullptr;
    }


    if (ext2fs_ilock(ip) != 0) {
        ext2fs_iput(ip);
        ext2_unlock(dev_copy);
        return nullptr;
    }

    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    if (!new_node) {
        ext2fs_iunlock(ip);
        ext2_unlock(dev_copy);
        ext2fs_iput(ip);
        return nullptr;
    }
    memset(new_node, 0, sizeof(vfs_inode_t));
    new_node->inode = ip->inum;
    new_node->size  = ip->size;
    new_node->flags = (ip->type == T_DIR) ? VFS_DIRECTORY : VFS_FILE;

    new_node->device = ip;
    new_node->iops   = &ext2_vfs_ops;

    ext2fs_iunlock(ip);
    ext2_unlock(dev_copy);


    return new_node;
}

static vfs_dirent_t *ext2_vfs_readdir(const vfs_inode_t *node, uint32_t index)
{
    if (!node || !node->device)
        return nullptr;
    auto dp = (struct ext2_inode *)node->device;
    if (!dp)
        return nullptr;
    if (ext2fs_ilock(dp) != 0)
        return nullptr;

    struct ext2_dir_entry_2 de;
    uint32_t off   = 0;
    uint32_t count = 0;

    while (off < dp->size) {
        if (ext2_read_inode(dp, (char *)&de, off, 8) != 8)
            break;
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE)
            break;

        if (de.inode != 0) {
            if (count == index) {
                vfs_dirent_t *dent = kmalloc(sizeof(vfs_dirent_t));
                if (!dent) {
                    ext2fs_iunlock(dp);
                    return nullptr;
                }
                dent->inode = de.inode;

                int name_len = de.name_len;
                if (name_len > EXT2_NAME_LEN)
                    name_len = EXT2_NAME_LEN;
                ext2_read_inode(dp, dent->name, off + 8, name_len);
                assert(name_len < 128, "ext2_vfs_readdir: name_len too large");
                dent->name[name_len] = 0;

                ext2fs_iunlock(dp);
                return dent;
            }
            count++;
        }
        off += de.rec_len;
    }

    ext2fs_iunlock(dp);
    return nullptr;
}

static int ext2_vfs_mknod(const struct vfs_inode *node, const char *name, const int mode, const int dev)
{
    if (!node || !name)
        return -EINVAL;
    if ((node->flags & VFS_TYPE_MASK) != VFS_DIRECTORY)
        return -ENOTDIR;
    if (mode != VFS_FILE && mode != VFS_DIRECTORY && mode != VFS_CHARDEVICE && mode != VFS_BLOCKDEVICE)
        return -EINVAL;

    struct ext2_inode *parent_inode = (struct ext2_inode *)node->device;
    if (!parent_inode)
        return -EIO;
    const uint32_t dev_id = parent_inode->dev;
    ext2_lock(dev_id);
    if (ext2fs_ilock(parent_inode) != 0) {
        ext2_unlock(dev_id);
        return -EIO;
    }

    struct ext2_inode *ip = ext2fs_dirlookup(parent_inode, name, nullptr);
    if (ip) {
        ext2fs_iput(ip);
        ext2fs_iunlock(parent_inode);
        ext2_unlock(dev_id);
        return -EINSTKN;
    }

    short ext2_type = EXT2_FT_REG_FILE;
    if (mode == VFS_CHARDEVICE)
        ext2_type = EXT2_FT_CHRDEV;
    else if (mode == VFS_BLOCKDEVICE)
        ext2_type = EXT2_FT_BLKDEV;
    else if (mode == VFS_DIRECTORY)
        ext2_type = EXT2_FT_DIR;

    ip = ext2fs_ialloc(parent_inode->dev, ext2_type);
    if (!ip) {
        printk("ext2_vfs_mknod: ialloc failed\n");
        ext2fs_iunlock(parent_inode);
        ext2_unlock(dev_id);
        return -ENOMEM;
    }

    ip->major = (dev >> 8) & 0xFF;
    ip->minor = dev & 0xFF;
    ip->nlink = 1;
    ext2fs_iupdate(ip);

    if (ext2fs_dirlink(parent_inode, name, ip->inum) < 0) {
        printk("ext2_vfs_mknod: dirlink failed\n");
        ext2fs_iput(ip);
        ext2fs_iunlock(parent_inode);
        ext2_unlock(dev_id);
        return -EIO;
    }

    ext2fs_iput(ip);
    ext2fs_iunlock(parent_inode);
    ext2_unlock(dev_id);
    return 0;
}

static vfs_inode_t *ext2_vfs_clone(const vfs_inode_t *node)
{
    if (!node)
        return nullptr;

    const struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (!ip)
        return nullptr;
    struct ext2_inode *new_ip = iget(ip->dev, ip->inum);

    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    if (!new_node) {
        ext2fs_iput(new_ip);
        return nullptr;
    }
    memcpy(new_node, node, sizeof(vfs_inode_t));
    new_node->device = new_ip;
    return new_node;
}

static int ext2_vfs_stat(const vfs_inode_t *node, struct stat *st)
{
    if (!node || !st)
        return -1;

    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (!ip)
        return -1;

    if (ext2fs_ilock(ip) != 0)
        return -1;

    ext2_stat_inode(ip, st);

    ext2fs_iunlock(ip);
    return 0;
}

static struct inode_operations ext2_vfs_ops = {
    .read     = ext2_vfs_read,
    .write    = ext2_vfs_write,
    .truncate = ext2_vfs_truncate,
    .open     = ext2_vfs_open,
    .close    = ext2_vfs_close,
    .readdir  = ext2_vfs_readdir,
    .finddir  = ext2_vfs_finddir,
    .mknod    = ext2_vfs_mknod,
    .clone    = ext2_vfs_clone,
    .link     = ext2_vfs_link,
    .unlink   = ext2_vfs_unlink,
    .stat     = ext2_vfs_stat,
    .rename   = ext2_vfs_rename,
};

vfs_inode_t *ext2_mount(uint8_t drive_index, uint32_t partition_lba)
{
    static bool initialized = false;
    if (!initialized) {
        memset(&icache, 0, sizeof(icache));
        spinlock_init(&icache.lock);
        for (int i = 0; i < NINODE; i++)
            sleeplock_init(&icache.inode[i].lock, "inode");
        ext2_dev_locks_init();
        initialized = true;
    }

    first_partition_blocks[drive_index] = partition_lba;
    ext2fs_readsb(drive_index, ext2_get_sb(drive_index));

    // Reject a corrupt superblock: these are divisors throughout the driver.
    const struct ext2_super_block *sb = ext2_get_sb(drive_index);
    if (sb->s_blocks_per_group == 0 || sb->s_inodes_per_group == 0 || sb->s_inode_size == 0) {
        printk("ext2_mount: invalid superblock (zero per-group count or inode size)\n");
        return nullptr;
    }

    struct ext2_inode *root_ip = iget(drive_index, 2);
    if (ext2fs_ilock(root_ip) != 0) {
        ext2fs_iput(root_ip);
        return nullptr;
    }

    vfs_inode_t *root = kmalloc(sizeof(vfs_inode_t));
    if (!root) {
        ext2fs_iunlockput(root_ip);
        return nullptr;
    }
    memset(root, 0, sizeof(vfs_inode_t));
    root->inode  = 2;
    root->flags  = VFS_DIRECTORY;
    root->device = root_ip;
    root->iops   = &ext2_vfs_ops;

    ext2fs_iunlock(root_ip);

    return root;
}
