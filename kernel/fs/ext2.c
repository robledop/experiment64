#include "ext2.h"
#include <string.h>
#include <stdint.h>
#include "vfs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "bio.h"
#include "terminal.h"
#include "heap.h"
#include "debug.h"
#include <limits.h>
#include "util.h"
#ifdef KASAN
#include "kasan.h"
#endif

#ifndef ASSERT
#define ASSERT(c, msg) \
    if (!(c))          \
    {                  \
        panic(msg);    \
    }
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define MAX_FILE_PATH 256

// Map internal types to VFS types or just use constants
#define T_DIR EXT2_FT_DIR
#define T_FILE EXT2_FT_REG_FILE
#define T_DEV EXT2_FT_CHRDEV

#define NDEV 10 // Dummy value

#define min(a, b) ((a) < (b) ? (a) : (b))

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
struct ext2_super_block ext2_sb;

// part_offset is kept for back-compat; prefer ext2_part_offset().
static inline uint32_t part_offset(uint32_t dev)
{
    return ext2_part_offset(dev);
}
// extern struct mbr mbr; // Removed dependency

void ext2fs_readsb(int dev, struct ext2_super_block *sb)
{
    // Superblock starts at byte 1024 (Sector 2)
    const uint32_t sb_blockno = part_offset(dev) + 2;

    buffer_head_t *bp = bread(dev, sb_blockno);
    if (!bp || !bp->data)
    {
        panic("ext2fs_readsb: failed to read superblock");
    }
    memcpy(sb, bp->data, 512);
    brelse(bp);

    bp = bread(dev, sb_blockno + 1);
    if (!bp || !bp->data)
    {
        panic("ext2fs_readsb: failed to read superblock (second half)");
    }
    memcpy((uint8_t *)sb + 512, bp->data, 512);
    brelse(bp);

    boot_message(INFO, "EXT2: Magic: %x, Inode Size: %d, Block Size: %d", sb->s_magic, sb->s_inode_size,
                 1024 << sb->s_log_block_size);
}

struct icache icache;

static struct ext2_inode *iget(uint32_t dev, uint32_t inum)
{
    struct ext2_inode *ip;

    spinlock_acquire(&icache.lock);

    // Is the inode already cached?
    struct ext2_inode *empty = nullptr;
    for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++)
    {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum)
        {
            ip->ref++;
            spinlock_release(&icache.lock);
            return ip;
        }
        if (empty == nullptr && ip->ref == 0) // Remember empty slot.
            empty = ip;
    }

    // Recycle an inode cache entry.
    if (empty == nullptr)
    {
        panic("iget: no inodes");
    }

    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    ip->type = 0;
    ip->size = 0;
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
    uint32_t *ptrs = (uint32_t *)bp->data;

    for (uint32_t i = 0; i < EXT2_INDIRECT; i++)
    {
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
static uint32_t ext2fs_get_free_bit(uint8_t *bitmap, const uint32_t nbits)
{
    const uint32_t bytes = (nbits + 7) / 8;
    for (uint32_t i = 0; i < bytes && i < EXT2_BSIZE; i++)
    {
        for (uint32_t j = 0; j < 8; j++)
        {
            const uint32_t bit = i * 8 + j;
            if (bit >= nbits)
                break;
            const u8 mask = (u8)(1U << j);
            if ((bitmap[i] & mask) != 0)
                continue;
            bitmap[i] |= mask;
            return bit;
        }
    }
    return (uint32_t)-1;
}

static inline void ext2_read_group_desc(uint32_t dev, int gno, struct ext2_group_desc *out)
{
    const uint32_t desc_blockno = ext2_part_offset(dev) + BLOCK_TO_SECTOR(2);
    buffer_head_t *bp = bread(dev, desc_blockno);
    memcpy(out, bp->data + gno * sizeof(*out), sizeof(*out));
    brelse(bp);
}

// Read a pointer entry from a block and allocate it if empty.
static uint32_t ext2_ensure_ptr(uint32_t dev, uint32_t block, uint32_t slot, uint32_t inum)
{
    const uint32_t sector = BLOCK_TO_SECTOR(block) + ext2_part_offset(dev) + (slot / PTRS_PER_SECTOR);
    buffer_head_t *bp = bread(dev, sector);
    uint32_t *table = (uint32_t *)bp->data;
    uint32_t *entry = &table[slot % PTRS_PER_SECTOR];
    uint32_t val = *entry;
    if (val == 0)
    {
        val = ext2fs_balloc(dev, inum);
        *entry = val;
        bwrite(bp);
    }
    brelse(bp);
    return val;
}

// Allocate a zeroed disk block.
static uint32_t ext2fs_balloc(uint32_t dev, uint32_t inum)
{
    const int gno = GET_GROUP_NO(inum, ext2_sb);
    struct ext2_group_desc bgdesc;
    ext2_read_group_desc(dev, gno, &bgdesc);

    const uint32_t bitmap_block = BLOCK_TO_SECTOR(bgdesc.bg_block_bitmap) + ext2_part_offset(dev);
    buffer_head_t *bp = bread(dev, bitmap_block);

    const uint32_t fbit = ext2fs_get_free_bit((uint8_t *)bp->data, ext2_sb.s_blocks_per_group);
    if (fbit == (uint32_t)-1)
    {
        brelse(bp);
        printk("PANIC: ");
        printk("ext2_balloc: out of blocks\n");
        return 0;
    }

    bwrite(bp);
    brelse(bp);

    const uint32_t group_first_block = ext2_sb.s_first_data_block + gno * ext2_sb.s_blocks_per_group;
    const uint32_t rel_block = group_first_block + fbit;
    const uint32_t start_sector = BLOCK_TO_SECTOR(rel_block) + ext2_part_offset(dev);
    for (uint32_t i = 0; i < EXT2_BSIZE / 512; i++)
    {
        ext2fs_bzero(dev, start_sector + i);
    }
    return rel_block;
}

// Free a disk block.
static void ext2fs_bfree(uint32_t dev, uint32_t b)
{
    if (b < ext2_sb.s_first_data_block)
    {
        printk("PANIC: ");
        printk("ext2fs_bfree: invalid block\n");
    }

    const uint32_t block_index = b - ext2_sb.s_first_data_block;
    const uint32_t gno = block_index / ext2_sb.s_blocks_per_group;
    const uint32_t offset = block_index % ext2_sb.s_blocks_per_group;

    struct ext2_group_desc bgdesc;
    ext2_read_group_desc(dev, gno, &bgdesc);

    buffer_head_t *bp = bread(dev, BLOCK_TO_SECTOR(bgdesc.bg_block_bitmap) + ext2_part_offset(dev));
    const uint32_t byte_index = offset / 8;
    if (byte_index >= EXT2_BSIZE)
    {
        printk("PANIC: ");
        printk("ext2fs_bfree: bitmap overflow\n");
    }
    const u8 mask = (u8)(1U << (offset % 8));

    if ((bp->data[byte_index] & mask) == 0)
    {
        printk("PANIC: ");
        printk("ext2fs_bfree: block already free\n");
    }
    bp->data[byte_index] &= ~mask;
    bwrite(bp);
    brelse(bp);
}

void ext2_init_inode(int dev)
{
    // mbr_load();
    ext2fs_readsb(dev, &ext2_sb);
    const uint32_t block_bytes = 1024u << ext2_sb.s_log_block_size;
    const u64 partition_mb = ((u64)ext2_sb.s_blocks_count * block_bytes) / (1024ull * 1024ull);
    const u64 size_value = (partition_mb >= 1024ull) ? partition_mb / 1024ull : partition_mb;
    const char *size_suffix = (partition_mb >= 1024ull) ? "GB" : "MB";
    printk(
        "ext2: size: %llu %s, block_size: %u, block_count: %u, inodes: %u",
        (unsigned long long)size_value,
        size_suffix,
        block_bytes,
        ext2_sb.s_blocks_count,
        ext2_sb.s_inodes_count);
}

// Helper to compute sector and byte offset for an inode within its group's inode table.
// Returns the absolute sector number and the byte offset within that sector.
static void ext2_inode_loc(uint32_t dev, uint32_t inum, uint32_t *sector, uint32_t *byte_offset)
{
    const int gno = GET_GROUP_NO(inum, ext2_sb);
    const int ioff = GET_INODE_INDEX(inum, ext2_sb);
    struct ext2_group_desc bgdesc;
    ext2_read_group_desc(dev, gno, &bgdesc);

    const uint32_t inodes_per_block = EXT2_BSIZE / ext2_sb.s_inode_size;
    const uint32_t bno = BLOCK_TO_SECTOR(bgdesc.bg_inode_table + ioff / inodes_per_block) + ext2_part_offset(dev);
    const uint32_t iindex = ioff % inodes_per_block;

    const uint32_t block_off = iindex * ext2_sb.s_inode_size;
    *sector = bno + block_off / 512;
    *byte_offset = block_off % 512;
}

struct ext2_inode *ext2fs_ialloc(uint32_t dev, short type)
{
    struct ext2_group_desc bgdesc;
    const uint32_t desc_blockno = part_offset(dev) + BLOCK_TO_SECTOR(2);
    // block group descriptor table starts at block 2

    const uint32_t bgcount = ext2_sb.s_blocks_count / ext2_sb.s_blocks_per_group;
    for (uint32_t i = 0; i <= bgcount; i++)
    {
        buffer_head_t *group_desc_buf = bread(dev, desc_blockno);
        memcpy(&bgdesc, group_desc_buf->data + i * sizeof(bgdesc), sizeof(bgdesc));
        brelse(group_desc_buf);

        buffer_head_t *ibitmap_buff = bread(dev, BLOCK_TO_SECTOR(bgdesc.bg_inode_bitmap) + part_offset(dev));
        const uint32_t fbit = ext2fs_get_free_bit((uint8_t *)ibitmap_buff->data, ext2_sb.s_inodes_per_group);
        if (fbit == (uint32_t)-1)
        {
            brelse(ibitmap_buff);
            continue;
        }

        if (ext2_sb.s_inode_size == 0)
        {
            printk("PANIC: ");
            printk("ext2fs_ialloc: invalid inode size");
            brelse(ibitmap_buff);
            return nullptr;
        }

        const uint32_t inodes_per_block = EXT2_BSIZE / ext2_sb.s_inode_size;

        const uint32_t bno = BLOCK_TO_SECTOR(bgdesc.bg_inode_table + fbit / inodes_per_block) + part_offset(dev);
        const uint32_t iindex = fbit % inodes_per_block;

        const uint32_t block_offset = iindex * ext2_sb.s_inode_size;
        const uint32_t sector_offset = block_offset / 512;
        const uint32_t sector_byte_offset = block_offset % 512;

        buffer_head_t *dinode_buff = bread(dev, bno + sector_offset);
        u8 *slot = dinode_buff->data + sector_byte_offset;

        memset(slot, 0, ext2_sb.s_inode_size);
        struct ext2_disk_inode *din = (struct ext2_disk_inode *)slot;
        if (type == T_DIR)
        {
            din->i_mode = S_IFDIR;
        }
        else if (type == T_FILE)
        {
            din->i_mode = S_IFREG;
        }
        else if (type == T_DEV)
        {
            din->i_mode = S_IFCHR;
        }
        bwrite(dinode_buff);
        bwrite(ibitmap_buff);
        brelse(dinode_buff);
        brelse(ibitmap_buff);

        const uint32_t inum = i * ext2_sb.s_inodes_per_group + fbit + 1;
        struct ext2_inode *ip = iget(dev, inum);
        ip->type = type;
        return ip;
    }
    printk("PANIC: ");
    printk("ext2_ialloc: no inodes");
    return nullptr;
}

void ext2fs_iupdate(const struct ext2_inode *ip)
{
    uint32_t sector, sector_byte_offset;
    ext2_inode_loc(ip->dev, ip->inum, &sector, &sector_byte_offset);

    buffer_head_t *bp1 = bread(ip->dev, sector);
    if (ext2_sb.s_inode_size > EXT2_MAX_INODE_SIZE)
    {
        printk("PANIC: ");
        printk("ext2fs_iupdate: inode too large");
    }

    struct ext2_disk_inode *din = (struct ext2_disk_inode *)(bp1->data + sector_byte_offset);

    if (ip->type == T_DIR)
        din->i_mode = S_IFDIR;
    else if (ip->type == T_FILE)
        din->i_mode = S_IFREG;
    else if (ip->type == T_DEV)
        din->i_mode = S_IFCHR;

    din->i_atime = ip->i_atime;
    din->i_ctime = ip->i_ctime;
    din->i_mtime = ip->i_mtime;
    din->i_dtime = din->i_uid = din->i_gid = din->i_flags = din->i_generation = 0;
    din->i_links_count = ip->nlink;
    din->i_size = ip->size;

    const struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;
    memcpy(din->i_block, ad->addrs, sizeof(ad->addrs));

    if (ip->type == T_DEV)
    {
        din->i_block[0] = (ip->major << 8) | ip->minor;
    }

    // memcpy(bp1->data + sector_byte_offset, raw, ext2_sb.s_inode_size);
    bwrite(bp1);
    brelse(bp1);
}

int ext2fs_ilock(struct ext2_inode *ip)
{
    if (ip == nullptr || ip->ref < 1)
    {
        return -1;
    }

    ASSERT(ip->addrs != nullptr, "ip->addrs is null in ext2fs_ilock before lock");
    sleeplock_acquire(&ip->lock);
    if (ip->addrs == nullptr)
    {
        sleeplock_release(&ip->lock);
        return -1;
    }
    auto const ad = (struct ext2fs_addrs *)ip->addrs;

    if (ip->valid == 0)
    {
        uint32_t sector, sector_byte_offset;
        ext2_inode_loc(ip->dev, ip->inum, &sector, &sector_byte_offset);

        buffer_head_t *bp1 = bread(ip->dev, sector);
        if (!bp1)
        {
            sleeplock_release(&ip->lock);
            return -1;
        }
        if (ext2_sb.s_inode_size > EXT2_MAX_INODE_SIZE)
        {
            brelse(bp1);
            sleeplock_release(&ip->lock);
            return -1;
        }
        u8 raw[EXT2_MAX_INODE_SIZE];
        memcpy(raw, bp1->data + sector_byte_offset, ext2_sb.s_inode_size);
        brelse(bp1);

        const struct ext2_disk_inode *din = (struct ext2_disk_inode *)raw;

        if (S_ISDIR(din->i_mode) || din->i_mode == T_DIR)
        {
            ip->type = T_DIR;
        }
        else if (S_ISREG(din->i_mode))
        {
            ip->type = T_FILE;
        }
        else if (S_ISCHR(din->i_mode))
        {
            ip->type = T_DEV;
            ip->major = (din->i_block[0] >> 8) & 0xFF;
            ip->minor = din->i_block[0] & 0xFF;
        }
        ip->i_atime = din->i_atime;
        ip->i_ctime = din->i_ctime;
        ip->i_mtime = din->i_mtime;
        ip->i_dtime = din->i_dtime;
        ip->i_uid = din->i_uid;
        ip->i_gid = din->i_gid;
        ip->i_flags = din->i_flags;

        ip->nlink = din->i_links_count;
        ip->size = din->i_size;
        memcpy(ad->addrs, din->i_block, sizeof(ad->addrs));

        ip->valid = 1;
        if (ip->type == 0)
        {
            sleeplock_release(&ip->lock);
            return -1;
        }
    }

    if (ip->type == 0)
    {
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
    struct ext2_group_desc bgdesc;
    const uint32_t desc_blockno = part_offset(ip->dev) + BLOCK_TO_SECTOR(2);
    // block group descriptor table starts at block 2

    const int gno = GET_GROUP_NO(ip->inum, ext2_sb);
    buffer_head_t *bp1 = bread(ip->dev, desc_blockno);
    memcpy(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp1);
    buffer_head_t *bp2 = bread(ip->dev, BLOCK_TO_SECTOR(bgdesc.bg_inode_bitmap) + part_offset(ip->dev));
    const uint32_t index = (ip->inum - 1) % ext2_sb.s_inodes_per_group;
    const uint32_t byte_index = index / 8;
    if (byte_index >= EXT2_BSIZE)
    {
        printk("PANIC: ");
        printk("ext2fs_ifree: bitmap overflow\n");
    }
    const u8 mask = (u8)(1U << (index % 8));

    if ((bp2->data[byte_index] & mask) == 0)
    {
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

    if (ip->valid && ip->nlink == 0)
    {
        spinlock_acquire(&icache.lock);
        const int r = ip->ref;
        spinlock_release(&icache.lock);
        if (r == 1)
        {
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
    if (ip->ref == 0)
    {
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
    st->dev = clamp_to_int(ip->dev);
    st->ino = clamp_to_int(ip->inum);
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
    st->ref = ip->ref;
    st->i_atime = ip->i_atime;
    st->i_ctime = ip->i_ctime;
    st->i_mtime = ip->i_mtime;
    st->i_dtime = ip->i_dtime;
    st->i_uid = ip->i_uid;
    st->i_gid = ip->i_gid;
    st->i_flags = clamp_to_int(ip->i_flags);
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

    if (bn < EXT2_NDIR_BLOCKS)
    {
        if (ad->addrs[bn] == 0)
        {
            ad->addrs[bn] = ext2fs_balloc(ip->dev, ip->inum);
        }
        return BLOCK_TO_SECTOR(ad->addrs[bn]) + ext2_part_offset(ip->dev);
    }
    bn -= EXT2_NDIR_BLOCKS;
    if (bn < EXT2_INDIRECT)
    {
        if (ad->addrs[EXT2_IND_BLOCK] == 0)
        {
            ad->addrs[EXT2_IND_BLOCK] = ext2fs_balloc(ip->dev, ip->inum);
        }
        const uint32_t entry = ext2_ensure_ptr(ip->dev, ad->addrs[EXT2_IND_BLOCK], bn, ip->inum);
        return BLOCK_TO_SECTOR(entry) + ext2_part_offset(ip->dev);
    }
    bn -= EXT2_INDIRECT;

    if (bn < EXT2_DINDIRECT)
    {
        if (ad->addrs[EXT2_DIND_BLOCK] == 0)
        {
            ad->addrs[EXT2_DIND_BLOCK] = ext2fs_balloc(ip->dev, ip->inum);
        }

        const uint32_t first_index = bn / EXT2_INDIRECT;
        const uint32_t second_index = bn % EXT2_INDIRECT;

        const uint32_t mid = ext2_ensure_ptr(ip->dev, ad->addrs[EXT2_DIND_BLOCK], first_index, ip->inum);
        const uint32_t leaf = ext2_ensure_ptr(ip->dev, mid, second_index, ip->inum);
        return BLOCK_TO_SECTOR(leaf) + ext2_part_offset(ip->dev);
    }
    bn -= EXT2_DINDIRECT;

    if (bn < EXT2_TINDIRECT)
    {
        if (ad->addrs[EXT2_TIND_BLOCK] == 0)
        {
            ad->addrs[EXT2_TIND_BLOCK] = ext2fs_balloc(ip->dev, ip->inum);
        }

        const uint32_t first_index = bn / EXT2_DINDIRECT;
        const uint32_t remainder = bn % EXT2_DINDIRECT;
        const uint32_t second_index = remainder / EXT2_INDIRECT;
        const uint32_t third_index = remainder % EXT2_INDIRECT;

        const uint32_t level1 = ext2_ensure_ptr(ip->dev, ad->addrs[EXT2_TIND_BLOCK], first_index, ip->inum);
        const uint32_t level2 = ext2_ensure_ptr(ip->dev, level1, second_index, ip->inum);
        const uint32_t leaf = ext2_ensure_ptr(ip->dev, level2, third_index, ip->inum);
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

    for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (ad->addrs[i])
        {
            ext2fs_bfree(ip->dev, ad->addrs[i]);
            ad->addrs[i] = 0;
        }
    }

    if (ad->addrs[EXT2_IND_BLOCK])
    {
        ext2_free_indirect(ip->dev, ad->addrs[EXT2_IND_BLOCK], 1);
        ad->addrs[EXT2_IND_BLOCK] = 0;
    }

    if (ad->addrs[EXT2_DIND_BLOCK])
    {
        ext2_free_indirect(ip->dev, ad->addrs[EXT2_DIND_BLOCK], 2);
        ad->addrs[EXT2_DIND_BLOCK] = 0;
    }

    if (ad->addrs[EXT2_TIND_BLOCK])
    {
        ext2_free_indirect(ip->dev, ad->addrs[EXT2_TIND_BLOCK], 3);
        ad->addrs[EXT2_TIND_BLOCK] = 0;
    }

    ip->size = 0;
    ext2fs_iupdate(ip);
}

int ext2_read_inode(const struct ext2_inode *ip, char *dst, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV)
    {
        return -1;
    }

    if (off >= ip->size || off + n < off)
    {
        return 0;
    }
    if (off + n > ip->size)
    {
        n = ip->size - off;
    }
    for (uint32_t tot = 0; tot < n;)
    {
        const uint32_t logical_block = off / EXT2_BSIZE;
        const uint32_t sector_start = ext2fs_bmap(ip, logical_block); // Returns starting sector
        const uint32_t offset_in_block = off % EXT2_BSIZE;

        const uint32_t sector_offset = offset_in_block / 512;
        const uint32_t sector = sector_start + sector_offset;

        buffer_head_t *bp = bread(ip->dev, sector);
        if (!bp)
        {
            return -1;
        }

        const uint32_t offset_in_sector = offset_in_block % 512;
        const uint32_t bytes_to_copy = min(n - tot, 512 - offset_in_sector);

#ifdef KASAN
        if (kasan_is_ready())
            kasan_unpoison_range(dst, bytes_to_copy);
#endif
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
    if (ip->type == T_DEV)
    {
        return -1;
    }

    if (off > ip->size || off + n < off)
    {
        return -1;
    }
    if ((uint64_t)off + n > (uint64_t)EXT2_MAXFILE * EXT2_BSIZE)
    {
        return -1;
    }

    for (uint32_t tot = 0; tot < n;)
    {
        const uint32_t logical_block = off / EXT2_BSIZE;
        const uint32_t sector_start = ext2fs_bmap(ip, logical_block); // Returns starting sector
        const uint32_t offset_in_block = off % EXT2_BSIZE;

        const uint32_t sector_offset = offset_in_block / 512;
        const uint32_t sector = sector_start + sector_offset;

        buffer_head_t *bp = bread(ip->dev, sector);
        if (!bp)
        {
            return -1;
        }

        const uint32_t offset_in_sector = offset_in_block % 512;
        const uint32_t bytes_to_copy = min(n - tot, 512 - offset_in_sector);

        memcpy(bp->data + offset_in_sector, src, bytes_to_copy);
        bwrite(bp);
        brelse(bp);

        tot += bytes_to_copy;
        off += bytes_to_copy;
        src += bytes_to_copy;
    }

    if (n > 0)
    {
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

    for (uint32_t off = 0; off + 8 <= dp->size;)
    {
        if (ext2_read_inode(dp, (char *)&de, off, 8) != 8)
            break;
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE || off + de.rec_len > dp->size)
        {
            panic("ext2fs_dirlookup: bad rec_len");
        }

        if (de.inode == 0)
        {
            off += de.rec_len;
            continue;
        }

        int to_copy = de.name_len;
        if (to_copy > EXT2_NAME_LEN)
            to_copy = EXT2_NAME_LEN;
        if (to_copy > 0 && ext2_read_inode(dp, (char *)de.name, off + 8, to_copy) != to_copy)
        {
            panic("ext2fs_dirlookup: name read");
        }
        memcpy(file_name, de.name, to_copy);
        file_name[to_copy] = '\0';

        if (ext2fs_namecmp(name, file_name) == 0)
        {
            if (poff)
            {
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
    if (name == nullptr)
    {
        return -1;
    }

    const size_t name_len = strlen(name);
    if (name_len == 0 || name_len > EXT2_NAME_LEN)
    {
        return -1;
    }

    struct ext2_dir_entry_2 de;
    struct ext2_inode *ip;

    if ((ip = ext2fs_dirlookup(dp, name, nullptr)) != nullptr)
    {
        ext2fs_iput(ip);
        return -1;
    }

    const uint32_t off = dp->size;
    const uint16_t rec_len = ext2_dirent_size((u8)name_len);

    memset(&de, 0, sizeof(de));
    de.inode = inum;
    de.rec_len = rec_len;
    de.name_len = (uint8_t)name_len;
    de.file_type = EXT2_FT_UNKNOWN;
    memcpy(de.name, name, name_len);

    if (ext2_write_inode(dp, (char *)&de, off, rec_len) != rec_len)
    {
        printk("ext2fs_dirlink: writei failed\n");
        return -1;
    }

    // Grow directory size to include the new entry and persist it.
    if (off + rec_len > dp->size)
    {
        dp->size = off + rec_len;
        ext2fs_iupdate(dp);
    }

    return 0;
}

// VFS Wrappers

static uint64_t ext2_vfs_read(const vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (ext2fs_ilock(ip) != 0)
        return 0;
    const int n = ext2_read_inode(ip, (char *)buffer, offset, size);
    ext2fs_iunlock(ip);
    return n > 0 ? n : 0;
}

static uint64_t ext2_vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (ext2fs_ilock(ip) != 0)
        return 0;
    const int n = ext2_write_inode(ip, (char *)buffer, offset, size);
    if (n > 0)
        node->size = ip->size;
    ext2fs_iunlock(ip);
    return n > 0 ? n : 0;
}

static int ext2_vfs_truncate(vfs_inode_t *node)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (!ip)
        return -1;

    if (ext2fs_ilock(ip) != 0)
        return -1;
    ext2fs_itrunc(ip);
    ext2fs_iunlock(ip);
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
    if (ip)
    {
        ext2fs_iput(ip);
        node->device = nullptr;
    }
}

static struct inode_operations ext2_vfs_ops;

static int ext2_vfs_link(vfs_inode_t *parent, const char *name, vfs_inode_t *target)
{
    if (!parent || !target || !name)
        return -1;
    if ((parent->flags & 0x07) != VFS_DIRECTORY)
        return -1;

    struct ext2_inode *dp = (struct ext2_inode *)parent->device;
    struct ext2_inode *ip = (struct ext2_inode *)target->device;
    if (!dp || !ip)
        return -1;

    if (ext2fs_ilock(dp) != 0)
        return -1;
    int res = ext2fs_dirlink(dp, name, ip->inum);
    ext2fs_iunlock(dp);
    if (res < 0)
        return -1;

    if (ext2fs_ilock(ip) != 0)
        return -1;
    ip->nlink++;
    ext2fs_iupdate(ip);
    ext2fs_iunlock(ip);
    return 0;
}

static int ext2_vfs_unlink(vfs_inode_t *parent, const char *name)
{
    if (!parent || !name)
        return -1;
    if ((parent->flags & 0x07) != VFS_DIRECTORY)
        return -1;

    struct ext2_inode *dp = (struct ext2_inode *)parent->device;
    if (!dp)
        return -1;

    if (ext2fs_ilock(dp) != 0)
        return -1;
    uint32_t off = 0;
    struct ext2_inode *ip = ext2fs_dirlookup(dp, name, &off);
    if (!ip)
    {
        ext2fs_iunlock(dp);
        return -1;
    }

    // Do not allow unlinking directories.
    if (ip->type == T_DIR)
    {
        ext2fs_iput(ip);
        ext2fs_iunlock(dp);
        return -1;
    }

    // Zero the inode field of the directory entry.
    uint32_t zero = 0;
    if (ext2_write_inode(dp, (char *)&zero, off, sizeof(zero)) != sizeof(zero))
    {
        ext2fs_iput(ip);
        ext2fs_iunlock(dp);
        return -1;
    }
    ext2fs_iunlock(dp);

    if (ext2fs_ilock(ip) != 0)
    {
        ext2fs_iput(ip);
        return -1;
    }
    if (ip->nlink > 0)
        ip->nlink--;
    ext2fs_iupdate(ip);
    ext2fs_iunlock(ip);
    ext2fs_iput(ip);
    return 0;
}

static vfs_inode_t *ext2_vfs_finddir(const vfs_inode_t *node, const char *name)
{
    struct ext2_inode *dp = (struct ext2_inode *)node->device;
    if (ext2fs_ilock(dp) != 0)
        return nullptr;
    // Note: ext2fs_dirlookup uses ext2fs_readi.
    // But we removed iops from struct ext2_inode.
    // We need to fix ext2fs_dirlookup to call ext2fs_readi directly.
    // Or restore iops but point to internal functions.
    // Since we are modifying ext2.c, we should fix the calls.
    // I will fix ext2fs_dirlookup and ext2fs_dirlink later.
    // For now assuming they call ext2fs_readi directly.

    struct ext2_inode *ip = ext2fs_dirlookup(dp, name, nullptr);
    ext2fs_iunlock(dp);

    if (!ip)
        return nullptr;

    if (ext2fs_ilock(ip) != 0)
    {
        ext2fs_iput(ip);
        return nullptr;
    }

    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    memset(new_node, 0, sizeof(vfs_inode_t));
    new_node->inode = ip->inum;
    new_node->size = ip->size;
    if (ip->type == T_DIR)
    {
        new_node->flags = VFS_DIRECTORY;
    }
    else
    {
        new_node->flags = VFS_FILE;
    }

    new_node->device = ip;
    new_node->iops = &ext2_vfs_ops;

    ext2fs_iunlock(ip);
    return new_node;
}

static vfs_dirent_t *ext2_vfs_readdir(const vfs_inode_t *node, uint32_t index)
{
    struct ext2_inode *dp = (struct ext2_inode *)node->device;
    if (ext2fs_ilock(dp) != 0)
        return nullptr;

    struct ext2_dir_entry_2 de;
    uint32_t off = 0;
    uint32_t count = 0;

    while (off < dp->size)
    {
        if (ext2_read_inode(dp, (char *)&de, off, 8) != 8)
            break;
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE)
            break;

        if (de.inode != 0)
        {
            if (count == index)
            {
                vfs_dirent_t *dent = kmalloc(sizeof(vfs_dirent_t));
                dent->inode = de.inode;

                int name_len = de.name_len;
                if (name_len > EXT2_NAME_LEN)
                    name_len = EXT2_NAME_LEN;
                ext2_read_inode(dp, dent->name, off + 8, name_len);
                ASSERT(name_len < 128, "ext2_vfs_readdir: name_len too large");
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
    struct ext2_inode *parent_inode = (struct ext2_inode *)node->device;
    if (ext2fs_ilock(parent_inode) != 0)
        return -1;

    struct ext2_inode *ip = ext2fs_dirlookup(parent_inode, name, nullptr);
    if (ip)
    {
        ext2fs_iput(ip);
        ext2fs_iunlock(parent_inode);
        return -1; // Exists
    }

    short ext2_type = EXT2_FT_REG_FILE;
    if (mode == VFS_CHARDEVICE)
        ext2_type = EXT2_FT_CHRDEV;
    else if (mode == VFS_BLOCKDEVICE)
        ext2_type = EXT2_FT_BLKDEV;
    else if (mode == VFS_DIRECTORY)
        ext2_type = EXT2_FT_DIR;

    ip = ext2fs_ialloc(parent_inode->dev, ext2_type);
    if (!ip)
    {
        printk("ext2_vfs_mknod: ialloc failed\n");
        ext2fs_iunlock(parent_inode);
        return -1;
    }

    ip->major = (dev >> 8) & 0xFF;
    ip->minor = dev & 0xFF;
    ip->nlink = 1;
    ext2fs_iupdate(ip);

    if (ext2fs_dirlink(parent_inode, name, ip->inum) < 0)
    {
        printk("ext2_vfs_mknod: dirlink failed\n");
        ext2fs_iput(ip);
        ext2fs_iunlock(parent_inode);
        return -1;
    }

    ext2fs_iput(ip);
    ext2fs_iunlock(parent_inode);
    return 0;
}

static vfs_inode_t *ext2_vfs_clone(const vfs_inode_t *node)
{
    const struct ext2_inode *ip = (struct ext2_inode *)node->device;
    struct ext2_inode *new_ip = iget(ip->dev, ip->inum);

    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    memcpy(new_node, node, sizeof(vfs_inode_t));
    new_node->device = new_ip;
    return new_node;
}

static struct inode_operations ext2_vfs_ops = {
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .truncate = ext2_vfs_truncate,
    .open = ext2_vfs_open,
    .close = ext2_vfs_close,
    .readdir = ext2_vfs_readdir,
    .finddir = ext2_vfs_finddir,
    .mknod = ext2_vfs_mknod,
    .clone = ext2_vfs_clone,
    .link = ext2_vfs_link,
    .unlink = ext2_vfs_unlink,
};

vfs_inode_t *ext2_mount(uint8_t drive_index, uint32_t partition_lba)
{
    static bool initialized = false;
    if (!initialized)
    {
        memset(&icache, 0, sizeof(icache));
        spinlock_init(&icache.lock);
        for (int i = 0; i < NINODE; i++)
            sleeplock_init(&icache.inode[i].lock, "inode");
        initialized = true;
    }

    first_partition_blocks[drive_index] = partition_lba;
    ext2fs_readsb(drive_index, &ext2_sb);

    struct ext2_inode *root_ip = iget(drive_index, 2);
    if (ext2fs_ilock(root_ip) != 0)
    {
        ext2fs_iput(root_ip);
        return nullptr;
    }

    vfs_inode_t *root = kmalloc(sizeof(vfs_inode_t));
    memset(root, 0, sizeof(vfs_inode_t));
    root->inode = 2;
    root->flags = VFS_DIRECTORY;
    root->device = root_ip;
    root->iops = &ext2_vfs_ops;

    ext2fs_iunlock(root_ip);

    return root;
}
