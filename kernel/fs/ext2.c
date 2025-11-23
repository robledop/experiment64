#include "ext2.h"
#include <string.h>
#include <stdint.h>
#include "vfs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "bio.h"
#include "terminal.h"
#include "heap.h"

#ifndef ASSERT
#define ASSERT(c, msg)              \
    if (!(c))                       \
    {                               \
        printf("PANIC: %s\n", msg); \
        while (1)                   \
            ;                       \
    }
#endif

void panic(const char *msg)
{
    printf("PANIC: %s\n", msg);
    while (1)
        ;
}

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

static void ext2fs_bzero(int dev, int bno);
static uint32_t ext2fs_balloc(uint32_t dev, uint32_t inum);
static void ext2fs_bfree(int dev, uint32_t b);
static uint32_t ext2fs_bmap(struct ext2_inode *ip, uint32_t bn);
static void ext2fs_itrunc(struct ext2_inode *ip);
struct ext2fs_addrs ext2fs_addrs[NINODE];
struct ext2_super_block ext2_sb;
uint32_t first_partition_block = 0;
// extern struct mbr mbr; // Removed dependency

void ext2fs_readsb(int dev, struct ext2_super_block *sb)
{
    // Superblock starts at byte 1024 (Sector 2)
    uint32_t sb_blockno = first_partition_block + 2;

    buffer_head_t *bp = bread(dev, sb_blockno);
    memcpy(sb, bp->data, 512);
    brelse(bp);

    bp = bread(dev, sb_blockno + 1);
    memcpy((uint8_t *)sb + 512, bp->data, 512);
    brelse(bp);

    printf("EXT2: Magic: %x, Inode Size: %d, Block Size: %d\n", sb->s_magic, sb->s_inode_size, 1024 << sb->s_log_block_size);
}

struct icache icache;

static struct ext2_inode *iget(uint32_t dev, uint32_t inum)
{
    struct ext2_inode *ip, *empty;

    spinlock_acquire(&icache.lock);

    // Is the inode already cached?
    empty = 0;
    for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++)
    {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum)
        {
            ip->ref++;
            spinlock_release(&icache.lock);
            return ip;
        }
        if (empty == 0 && ip->ref == 0) // Remember empty slot.
            empty = ip;
    }

    // Recycle an inode cache entry.
    if (empty == 0)
    {
        panic("iget: no inodes");
    }

    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    ip->addrs = &ext2fs_addrs[ip - icache.inode];
    spinlock_release(&icache.lock);

    return ip;
}

// Zero a block.
static void ext2fs_bzero(int dev, int bno)
{
    buffer_head_t *bp = bread(dev, bno);
    memset(bp->data, 0, EXT2_BSIZE);
    bwrite(bp);
    brelse(bp);
}

// check if a block is free and return its bit number
static uint32_t ext2fs_get_free_bit(char *bitmap, uint32_t nbits)
{
    const uint32_t bytes = (nbits + 7) / 8;
    for (uint32_t i = 0; i < bytes && i < EXT2_BSIZE; i++)
    {
        for (uint32_t j = 0; j < 8; j++)
        {
            uint32_t bit = i * 8 + j;
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

// Allocate a zeroed disk block.
static uint32_t ext2fs_balloc(uint32_t dev, uint32_t inum)
{
    struct ext2_group_desc bgdesc;
    uint32_t desc_blockno = first_partition_block + BLOCK_TO_SECTOR(2); // block group descriptor table starts at block 2

    int gno = GET_GROUP_NO(inum, ext2_sb);
    buffer_head_t *bp1 = bread(dev, desc_blockno);
    memcpy(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp1);
    buffer_head_t *bp2 = bread(dev, BLOCK_TO_SECTOR(bgdesc.bg_block_bitmap) + first_partition_block);

    uint32_t fbit = ext2fs_get_free_bit((char *)bp2->data, ext2_sb.s_blocks_per_group);
    if (fbit != (uint32_t)-1)
    {
        bwrite(bp2);
        brelse(bp2);

        uint32_t group_first_block = ext2_sb.s_first_data_block + gno * ext2_sb.s_blocks_per_group;
        uint32_t rel_block = group_first_block + fbit;
        ext2fs_bzero(dev, BLOCK_TO_SECTOR(rel_block) + first_partition_block);
        return rel_block;
    }
    brelse(bp2);
    printf("PANIC: ");
    printf("ext2_balloc: out of blocks\n");
}

// Free a disk block.
static void ext2fs_bfree(int dev, uint32_t b)
{
    struct ext2_group_desc bgdesc;
    uint32_t desc_blockno = first_partition_block + BLOCK_TO_SECTOR(2); // block group descriptor table starts at block 2

    if (b < ext2_sb.s_first_data_block)
    {
        printf("PANIC: ");
        printf("ext2fs_bfree: invalid block\n");
    }

    uint32_t block_index = b - ext2_sb.s_first_data_block;
    uint32_t gno = block_index / ext2_sb.s_blocks_per_group;
    uint32_t offset = block_index % ext2_sb.s_blocks_per_group;

    buffer_head_t *bp1 = bread(dev, desc_blockno);
    memcpy(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    buffer_head_t *bp2 = bread(dev, BLOCK_TO_SECTOR(bgdesc.bg_block_bitmap) + first_partition_block);
    uint32_t byte_index = offset / 8;
    if (byte_index >= EXT2_BSIZE)
    {
        printf("PANIC: ");
        printf("ext2fs_bfree: bitmap overflow\n");
    }
    u8 mask = (u8)(1U << (offset % 8));

    if ((bp2->data[byte_index] & mask) == 0)
    {
        printf("PANIC: ");
        printf("ext2fs_bfree: block already free\n");
    }
    bp2->data[byte_index] &= ~mask;
    bwrite(bp2);
    brelse(bp2);
    brelse(bp1);
}

void ext2fs_iinit(int dev)
{
    // mbr_load();
    ext2fs_readsb(dev, &ext2_sb);
    const uint32_t block_bytes = 1024u << ext2_sb.s_log_block_size;
    const u64 partition_mb = ((u64)ext2_sb.s_blocks_count * block_bytes) / (1024ull * 1024ull);
    const u64 size_value = (partition_mb >= 1024ull) ? partition_mb / 1024ull : partition_mb;
    const char *size_suffix = (partition_mb >= 1024ull) ? "GB" : "MB";
    printf(
        "ext2: size: %llu %s, block_size: %u, block_count: %u, inodes: %u",
        (unsigned long long)size_value,
        size_suffix,
        block_bytes,
        ext2_sb.s_blocks_count,
        ext2_sb.s_inodes_count);
}

struct ext2_inode *ext2fs_ialloc(uint32_t dev, short type)
{
    struct ext2_group_desc bgdesc;
    uint32_t desc_blockno = first_partition_block + BLOCK_TO_SECTOR(2); // block group descriptor table starts at block 2

    int bgcount = ext2_sb.s_blocks_count / ext2_sb.s_blocks_per_group;
    for (int i = 0; i <= bgcount; i++)
    {
        buffer_head_t *group_desc_buf = bread(dev, desc_blockno);
        memcpy(&bgdesc, group_desc_buf->data + i * sizeof(bgdesc), sizeof(bgdesc));
        brelse(group_desc_buf);

        buffer_head_t *ibitmap_buff = bread(dev, BLOCK_TO_SECTOR(bgdesc.bg_inode_bitmap) + first_partition_block);
        uint32_t fbit = ext2fs_get_free_bit((char *)ibitmap_buff->data, ext2_sb.s_inodes_per_group);
        if (fbit == (uint32_t)-1)
        {
            brelse(ibitmap_buff);
            continue;
        }

        int inodes_per_block = EXT2_BSIZE / ext2_sb.s_inode_size;
        if (inodes_per_block == 0)
        {
            printf("PANIC: ");
            printf("ext2fs_ialloc: invalid inode size");
        }

        int bno = BLOCK_TO_SECTOR(bgdesc.bg_inode_table + fbit / inodes_per_block) + first_partition_block;
        int iindex = fbit % inodes_per_block;

        uint32_t block_offset = iindex * ext2_sb.s_inode_size;
        uint32_t sector_offset = block_offset / 512;
        uint32_t sector_byte_offset = block_offset % 512;

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

        int inum = i * ext2_sb.s_inodes_per_group + fbit + 1;
        return iget(dev, inum);
    }
    printf("PANIC: ");
    printf("ext2_ialloc: no inodes");
}

void ext2fs_iupdate(struct ext2_inode *ip)
{
    struct ext2_group_desc bgdesc;
    uint32_t desc_blockno = first_partition_block + BLOCK_TO_SECTOR(2); // block group descriptor table starts at block 2

    int gno = GET_GROUP_NO(ip->inum, ext2_sb);
    int ioff = GET_INODE_INDEX(ip->inum, ext2_sb);
    buffer_head_t *bp = bread(ip->dev, desc_blockno);
    memcpy(&bgdesc, bp->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp);
    int bno = BLOCK_TO_SECTOR(bgdesc.bg_inode_table + ioff / (EXT2_BSIZE / ext2_sb.s_inode_size)) + first_partition_block;
    int iindex = ioff % (EXT2_BSIZE / ext2_sb.s_inode_size);

    uint32_t block_offset = iindex * ext2_sb.s_inode_size;
    uint32_t sector_offset = block_offset / 512;
    uint32_t sector_byte_offset = block_offset % 512;

    buffer_head_t *bp1 = bread(ip->dev, bno + sector_offset);
    if (ext2_sb.s_inode_size > EXT2_MAX_INODE_SIZE)
    {
        printf("PANIC: ");
        printf("ext2fs_iupdate: inode too large");
    }

    u8 raw[EXT2_MAX_INODE_SIZE];
    memcpy(raw, bp1->data + sector_byte_offset, ext2_sb.s_inode_size);
    struct ext2_disk_inode *din = (struct ext2_disk_inode *)raw;

    if (ip->type == T_DIR)
    {
        din->i_mode = S_IFDIR;
    }
    if (ip->type == T_FILE)
    {
        din->i_mode = S_IFREG;
    }
    if (ip->type == T_DEV)
    {
        din->i_mode = S_IFCHR;
    }
    din->i_links_count = ip->nlink;
    din->i_size = ip->size;
    din->i_dtime = 0;
    din->i_faddr = 0;
    din->i_file_acl = 0;
    din->i_flags = 0;
    din->i_generation = 0;
    din->i_gid = 0;
    din->i_mtime = 0;
    din->i_uid = 0;
    din->i_atime = 0;

    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;
    memcpy(din->i_block, ad->addrs, sizeof(ad->addrs));
    memcpy(bp1->data + sector_byte_offset, raw, ext2_sb.s_inode_size);
    bwrite(bp1);
    brelse(bp1);
}

void ext2fs_ilock(struct ext2_inode *ip)
{
    struct ext2_group_desc bgdesc;
    if (ip == nullptr || ip->ref < 1)
    {
        panic("ext2fs_ilock: invalid inode");
    }

    ASSERT(ip->addrs != nullptr, "ip->addrs is null in ext2fs_ilock before lock");
    sleeplock_acquire(&ip->lock);
    ASSERT(ip->addrs != nullptr, "ip->addrs is null in ext2fs_ilock");
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;
    uint32_t desc_block = first_partition_block + BLOCK_TO_SECTOR(2); // Group descriptor at ext2 block 2 = sector 4

    if (ip->valid == 0)
    {
        const int gno = GET_GROUP_NO(ip->inum, ext2_sb);
        const int ioff = GET_INODE_INDEX(ip->inum, ext2_sb);
        buffer_head_t *bp = bread(ip->dev, desc_block);
        memcpy(&bgdesc, bp->data + gno * sizeof(bgdesc), sizeof(bgdesc));
        brelse(bp);
        const int bno = BLOCK_TO_SECTOR(bgdesc.bg_inode_table + ioff / (EXT2_BSIZE / ext2_sb.s_inode_size)) + first_partition_block;
        const int iindex = ioff % (EXT2_BSIZE / ext2_sb.s_inode_size);

        uint32_t block_offset = iindex * ext2_sb.s_inode_size;
        uint32_t sector_offset = block_offset / 512;
        uint32_t sector_byte_offset = block_offset % 512;

        buffer_head_t *bp1 = bread(ip->dev, bno + sector_offset);
        if (ext2_sb.s_inode_size > EXT2_MAX_INODE_SIZE)
        {
            panic("ext2fs_ilock: inode too large");
        }
        u8 raw[EXT2_MAX_INODE_SIZE];
        memcpy(raw, bp1->data + sector_byte_offset, ext2_sb.s_inode_size);
        brelse(bp1);

        struct ext2_disk_inode *din = (struct ext2_disk_inode *)raw;

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
            panic("ext2fs_ilock: no type");
        }
    }
}

void ext2fs_iunlock(struct ext2_inode *ip)
{
    if (ip == nullptr || !sleeplock_holding(&ip->lock) || ip->ref < 1)
        panic("ext2fs_iunlock: invalid inode");

    sleeplock_release(&ip->lock);
}

// Free an inode
static void ext2fs_ifree(struct ext2_inode *ip)
{
    struct ext2_group_desc bgdesc;
    uint32_t desc_blockno = first_partition_block + BLOCK_TO_SECTOR(2); // block group descriptor table starts at block 2

    int gno = GET_GROUP_NO(ip->inum, ext2_sb);
    buffer_head_t *bp1 = bread(ip->dev, desc_blockno);
    memcpy(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp1);
    buffer_head_t *bp2 = bread(ip->dev, BLOCK_TO_SECTOR(bgdesc.bg_inode_bitmap) + first_partition_block);
    uint32_t index = (ip->inum - 1) % ext2_sb.s_inodes_per_group;
    uint32_t byte_index = index / 8;
    if (byte_index >= EXT2_BSIZE)
    {
        printf("PANIC: ");
        printf("ext2fs_ifree: bitmap overflow\n");
    }
    u8 mask = (u8)(1U << (index % 8));

    if ((bp2->data[byte_index] & mask) == 0)
    {
        printf("PANIC: ");
        printf("ext2fs_ifree: inode already free (inum=%u type=%d nlink=%d ref=%d)\n",
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
        int r = ip->ref;
        spinlock_release(&icache.lock);
        if (r == 1)
        {
            // inode has no links and no other references: truncate and free.
            ext2fs_ifree(ip);
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

void ext2fs_stati(struct ext2_inode *ip, struct stat *st)
{
    st->dev = (int)ip->dev;
    st->ino = ip->inum;
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
    st->i_flags = ip->i_flags;
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
static uint32_t ext2fs_bmap(struct ext2_inode *ip, uint32_t bn)
{
    uint32_t addr, *a, *b;
    buffer_head_t *bp, *bp1;
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    if (bn < EXT2_NDIR_BLOCKS)
    {
        if ((addr = ad->addrs[bn]) == 0)
        {
            addr = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[bn] = addr;
        }
        return BLOCK_TO_SECTOR(addr) + first_partition_block;
    }
    bn -= EXT2_NDIR_BLOCKS;
    if (bn < EXT2_INDIRECT)
    {
        if ((addr = ad->addrs[EXT2_IND_BLOCK]) == 0)
        {
            addr = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[EXT2_IND_BLOCK] = addr;
        }

        uint32_t sector_offset = bn / (512 / 4);
        uint32_t entry_index = bn % (512 / 4);

        bp = bread(ip->dev, first_partition_block + BLOCK_TO_SECTOR(addr) + sector_offset);
        a = (uint32_t *)bp->data;
        uint32_t entry = a[entry_index];
        if (entry == 0)
        {
            entry = ext2fs_balloc(ip->dev, ip->inum);
            a[entry_index] = entry;
            bwrite(bp);
        }
        brelse(bp);
        return BLOCK_TO_SECTOR(entry) + first_partition_block;
    }
    bn -= EXT2_INDIRECT;

    if (bn < EXT2_DINDIRECT)
    {
        if ((addr = ad->addrs[EXT2_DIND_BLOCK]) == 0)
        {
            addr = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[EXT2_DIND_BLOCK] = addr;
        }

        uint32_t first_index = bn / EXT2_INDIRECT;
        uint32_t first_sector_offset = first_index / (512 / 4);
        uint32_t first_entry_index = first_index % (512 / 4);

        bp = bread(ip->dev, first_partition_block + BLOCK_TO_SECTOR(addr) + first_sector_offset);
        a = (uint32_t *)bp->data;
        uint32_t entry = a[first_entry_index];
        if (entry == 0)
        {
            entry = ext2fs_balloc(ip->dev, ip->inum);
            a[first_entry_index] = entry;
            bwrite(bp);
        }
        brelse(bp);

        uint32_t second_index = bn % EXT2_INDIRECT;
        uint32_t second_sector_offset = second_index / (512 / 4);
        uint32_t second_entry_index = second_index % (512 / 4);

        bp1 = bread(ip->dev, first_partition_block + BLOCK_TO_SECTOR(entry) + second_sector_offset);
        b = (uint32_t *)bp1->data;
        uint32_t leaf = b[second_entry_index];
        if (leaf == 0)
        {
            leaf = ext2fs_balloc(ip->dev, ip->inum);
            b[second_entry_index] = leaf;
            bwrite(bp1);
        }
        brelse(bp1);
        return BLOCK_TO_SECTOR(leaf) + first_partition_block;
    }
    bn -= EXT2_DINDIRECT;

    if (bn < EXT2_TINDIRECT)
    {
        if ((addr = ad->addrs[EXT2_TIND_BLOCK]) == 0)
        {
            addr = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[EXT2_TIND_BLOCK] = addr;
        }
        bp = bread(ip->dev, first_partition_block + BLOCK_TO_SECTOR(addr));
        a = (uint32_t *)bp->data;
        uint32_t first_index = bn / EXT2_DINDIRECT;
        uint32_t entry = a[first_index];
        if (entry == 0)
        {
            entry = ext2fs_balloc(ip->dev, ip->inum);
            a[first_index] = entry;
            bwrite(bp);
        }
        brelse(bp);

        bp1 = bread(ip->dev, first_partition_block + BLOCK_TO_SECTOR(entry));
        b = (uint32_t *)bp1->data;
        uint32_t remainder = bn % EXT2_DINDIRECT;
        uint32_t second_idx = remainder / EXT2_INDIRECT;
        uint32_t mid = b[second_idx];
        if (mid == 0)
        {
            mid = ext2fs_balloc(ip->dev, ip->inum);
            b[second_idx] = mid;
            bwrite(bp1);
        }
        brelse(bp1);

        buffer_head_t *bp2 = bread(ip->dev, first_partition_block + mid);
        uint32_t *c = (uint32_t *)bp2->data;
        uint32_t third_idx = remainder % EXT2_INDIRECT;
        uint32_t leaf = c[third_idx];
        if (leaf == 0)
        {
            leaf = ext2fs_balloc(ip->dev, ip->inum);
            c[third_idx] = leaf;
            bwrite(bp2);
        }
        brelse(bp2);
        return BLOCK_TO_SECTOR(leaf) + first_partition_block;
    }
    printf("PANIC: ");
    printf("ext2_bmap: block number out of range\n");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void ext2fs_itrunc(struct ext2_inode *ip)
{
    uint32_t i, j;
    buffer_head_t *bp1, *bp2;
    uint32_t *a, *b;
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    // for direct blocks
    for (i = 0; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (ad->addrs[i])
        {
            ext2fs_bfree(ip->dev, ad->addrs[i]);
            ad->addrs[i] = 0;
        }
    }
    // EXT2_INDIRECT -> (EXT2_BSIZE / sizeof(uint32_t))
    // for indirect blocks
    if (ad->addrs[EXT2_IND_BLOCK])
    {
        bp1 = bread(ip->dev, ad->addrs[EXT2_IND_BLOCK] + first_partition_block);
        a = (uint32_t *)bp1->data;
        for (i = 0; i < EXT2_INDIRECT; i++)
        {
            if (a[i])
            {
                ext2fs_bfree(ip->dev, a[i]);
                a[i] = 0;
            }
        }
        brelse(bp1);
        ext2fs_bfree(ip->dev, ad->addrs[EXT2_IND_BLOCK]);
        ad->addrs[EXT2_IND_BLOCK] = 0;
    }

    // for double indirect blocks
    if (ad->addrs[EXT2_DIND_BLOCK])
    {
        bp1 = bread(ip->dev, ad->addrs[EXT2_DIND_BLOCK] + first_partition_block);
        a = (uint32_t *)bp1->data;
        for (i = 0; i < EXT2_INDIRECT; i++)
        {
            if (a[i])
            {
                bp2 = bread(ip->dev, a[i] + first_partition_block);
                b = (uint32_t *)bp2->data;
                for (j = 0; j < EXT2_INDIRECT; j++)
                {
                    if (b[j])
                    {
                        ext2fs_bfree(ip->dev, b[j]);
                        b[j] = 0;
                    }
                }
                brelse(bp2);
                ext2fs_bfree(ip->dev, a[i]);
                a[i] = 0;
            }
        }
        brelse(bp1);
        ext2fs_bfree(ip->dev, ad->addrs[EXT2_DIND_BLOCK]);
        ad->addrs[EXT2_DIND_BLOCK] = 0;
    }

    // for triple indirect blocks
    if (ad->addrs[EXT2_TIND_BLOCK])
    {
        bp1 = bread(ip->dev, ad->addrs[EXT2_TIND_BLOCK] + first_partition_block);
        a = (uint32_t *)bp1->data;
        for (i = 0; i < EXT2_INDIRECT; i++)
        {
            if (a[i])
            {
                bp2 = bread(ip->dev, a[i] + first_partition_block);
                b = (uint32_t *)bp2->data;
                for (j = 0; j < EXT2_INDIRECT; j++)
                {
                    if (b[j])
                    {
                        buffer_head_t *bp3 = bread(ip->dev, b[j] + first_partition_block);
                        uint32_t *c = (uint32_t *)bp3->data;
                        for (uint32_t k = 0; k < EXT2_INDIRECT; k++)
                        {
                            if (c[k])
                            {
                                ext2fs_bfree(ip->dev, c[k]);
                                c[k] = 0;
                            }
                        }
                        brelse(bp3);
                        ext2fs_bfree(ip->dev, b[j]);
                        b[j] = 0;
                    }
                }
                brelse(bp2);
                ext2fs_bfree(ip->dev, a[i]);
                a[i] = 0;
            }
        }
        brelse(bp1);
        ext2fs_bfree(ip->dev, ad->addrs[EXT2_TIND_BLOCK]);
        ad->addrs[EXT2_TIND_BLOCK] = 0;
    }

    ip->size = 0;
    ext2fs_iupdate(ip);
}

int ext2fs_readi(struct ext2_inode *ip, char *dst, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV)
    {
        return -1;
    }

    if (off > ip->size || off + n < off)
    {
        return -1;
    }
    if (off + n > ip->size)
    {
        n = ip->size - off;
    }

    for (uint32_t tot = 0; tot < n;)
    {
        uint32_t logical_block = off / EXT2_BSIZE;
        uint32_t sector_start = ext2fs_bmap(ip, logical_block); // Returns starting sector
        uint32_t offset_in_block = off % EXT2_BSIZE;

        uint32_t sector_offset = offset_in_block / 512;
        uint32_t sector = sector_start + sector_offset;

        buffer_head_t *bp = bread(ip->dev, sector);

        uint32_t offset_in_sector = offset_in_block % 512;
        uint32_t bytes_to_copy = min(n - tot, 512 - offset_in_sector);

        memcpy(dst, bp->data + offset_in_sector, bytes_to_copy);
        brelse(bp);

        tot += bytes_to_copy;
        off += bytes_to_copy;
        dst += bytes_to_copy;
    }
    return n;
}

int ext2fs_writei(struct ext2_inode *ip, char *src, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV)
    {
        return -1;
    }

    if (off > ip->size || off + n < off)
    {
        return -1;
    }
    if (off + n > EXT2_MAXFILE * EXT2_BSIZE)
    {
        return -1;
    }

    for (uint32_t tot = 0; tot < n;)
    {
        uint32_t logical_block = off / EXT2_BSIZE;
        uint32_t sector_start = ext2fs_bmap(ip, logical_block); // Returns starting sector
        uint32_t offset_in_block = off % EXT2_BSIZE;

        uint32_t sector_offset = offset_in_block / 512;
        uint32_t sector = sector_start + sector_offset;

        buffer_head_t *bp = bread(ip->dev, sector);

        uint32_t offset_in_sector = offset_in_block % 512;
        uint32_t bytes_to_copy = min(n - tot, 512 - offset_in_sector);

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
    return n;
}

int ext2fs_namecmp(const char *s, const char *t)
{
    return strncmp(s, t, EXT2_NAME_LEN);
}

static inline uint16_t ext2_dirent_size(u8 name_len)
{
    uint16_t size = 8 + name_len;
    return (size + 3) & ~3;
}

struct ext2_inode *ext2fs_dirlookup(struct ext2_inode *dp, char *name, uint32_t *poff)
{
    struct ext2_dir_entry_2 de;
    char file_name[EXT2_NAME_LEN + 1];
    for (uint32_t off = 0; off < dp->size;)
    {
        memset(&de, 0, sizeof(de));
        if (ext2fs_readi(dp, (char *)&de, off, 8) != 8)
        {
            printf("PANIC: ");
            printf("ext2fs_dirlookup: header read");
        }
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE)
        {
            panic("ext2fs_dirlookup: bad rec_len");
        }
        if (de.name_len > 0)
        {
            int to_copy = de.name_len;
            if (to_copy > EXT2_NAME_LEN)
            {
                to_copy = EXT2_NAME_LEN;
            }
            if (ext2fs_readi(dp, (char *)de.name, off + 8, to_copy) != to_copy)
            {
                panic("ext2fs_dirlookup: name read");
            }
        }
        if (de.inode == 0)
        {
            off += de.rec_len;
            continue;
        }
        if (de.name_len > EXT2_NAME_LEN)
        {
            panic("ext2fs_dirlookup: name too long");
        }
        memcpy(file_name, de.name, de.name_len);
        file_name[de.name_len] = '\0';
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

int ext2fs_dirlink(struct ext2_inode *dp, char *name, uint32_t inum)
{
    if (name == nullptr)
    {
        return -1;
    }

    int name_len = strlen(name);
    if (name_len <= 0 || name_len > EXT2_NAME_LEN)
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

    uint32_t off = dp->size;
    uint16_t rec_len = ext2_dirent_size((u8)name_len);

    memset(&de, 0, sizeof(de));
    de.inode = inum;
    de.rec_len = rec_len;
    de.name_len = name_len;
    de.file_type = EXT2_FT_UNKNOWN;
    memcpy(de.name, name, name_len);

    ext2fs_writei(dp, (char *)&de, off, rec_len);

    return 0;
}

// VFS Wrappers

static uint64_t ext2_vfs_read(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    ext2fs_ilock(ip);
    int n = ext2fs_readi(ip, (char *)buffer, offset, size);
    ext2fs_iunlock(ip);
    return n > 0 ? n : 0;
}

static uint64_t ext2_vfs_write(vfs_inode_t *node, uint64_t offset, uint64_t size, uint8_t *buffer)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    ext2fs_ilock(ip);
    int n = ext2fs_writei(ip, (char *)buffer, offset, size);
    ext2fs_iunlock(ip);
    return n > 0 ? n : 0;
}

static void ext2_vfs_open(vfs_inode_t *node)
{
    // Nothing to do
}

static void ext2_vfs_close(vfs_inode_t *node)
{
    struct ext2_inode *ip = (struct ext2_inode *)node->device;
    if (ip)
    {
        ext2fs_iput(ip);
        node->device = NULL;
    }
}

static struct inode_operations ext2_vfs_ops;

static vfs_inode_t *ext2_vfs_finddir(vfs_inode_t *node, char *name)
{
    struct ext2_inode *dp = (struct ext2_inode *)node->device;
    ext2fs_ilock(dp);
    // Note: ext2fs_dirlookup uses ext2fs_readi.
    // But we removed iops from struct ext2_inode.
    // We need to fix ext2fs_dirlookup to call ext2fs_readi directly.
    // Or restore iops but point to internal functions.
    // Since we are modifying ext2.c, we should fix the calls.
    // I will fix ext2fs_dirlookup and ext2fs_dirlink later.
    // For now assuming they call ext2fs_readi directly.

    struct ext2_inode *ip = ext2fs_dirlookup(dp, name, NULL);
    ext2fs_iunlock(dp);

    if (!ip)
        return NULL;

    ext2fs_ilock(ip);

    vfs_inode_t *new_node = kmalloc(sizeof(vfs_inode_t));
    memset(new_node, 0, sizeof(vfs_inode_t));
    new_node->inode = ip->inum;
    new_node->size = ip->size;
    if (ip->type == T_DIR)
        new_node->flags = VFS_DIRECTORY;
    else
        new_node->flags = VFS_FILE;

    new_node->device = ip;
    new_node->iops = &ext2_vfs_ops;

    ext2fs_iunlock(ip);
    return new_node;
}

static vfs_dirent_t *ext2_vfs_readdir(vfs_inode_t *node, uint32_t index)
{
    struct ext2_inode *dp = (struct ext2_inode *)node->device;
    ext2fs_ilock(dp);

    struct ext2_dir_entry_2 de;
    uint32_t off = 0;
    uint32_t count = 0;

    while (off < dp->size)
    {
        if (ext2fs_readi(dp, (char *)&de, off, 8) != 8)
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
                ext2fs_readi(dp, dent->name, off + 8, name_len);
                dent->name[name_len] = 0;

                ext2fs_iunlock(dp);
                return dent;
            }
            count++;
        }
        off += de.rec_len;
    }

    ext2fs_iunlock(dp);
    return NULL;
}

static struct inode_operations ext2_vfs_ops = {
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .open = ext2_vfs_open,
    .close = ext2_vfs_close,
    .readdir = ext2_vfs_readdir,
    .finddir = ext2_vfs_finddir,
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

    first_partition_block = partition_lba;
    ext2fs_readsb(drive_index, &ext2_sb);

    struct ext2_inode *root_ip = iget(drive_index, 2);
    ext2fs_ilock(root_ip);

    vfs_inode_t *root = kmalloc(sizeof(vfs_inode_t));
    memset(root, 0, sizeof(vfs_inode_t));
    root->inode = 2;
    root->flags = VFS_DIRECTORY;
    root->device = root_ip;
    root->iops = &ext2_vfs_ops;

    ext2fs_iunlock(root_ip);

    return root;
}