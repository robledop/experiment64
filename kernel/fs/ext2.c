#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "fs.h"
#include "ext2.h"
#include <string.h>
#include "assert.h"
#include "buf.h"
#include "file.h"
#include "icache.h"
#include "mbr.h"
#include <devtab.h>

struct inode_operations ext2fs_inode_ops = {
    ext2fs_dirlink,
    ext2fs_dirlookup,
    ext2fs_ialloc,
    ext2fs_iinit,
    ext2fs_ilock,
    ext2fs_iput,
    ext2fs_iunlock,
    ext2fs_iunlockput,
    ext2fs_iupdate,
    ext2fs_readi,
    ext2fs_stati,
    ext2fs_writei,
};

#define min(a,b) ((a) < (b) ? (a) : (b))

static void ext2fs_bzero(int dev, int bno);
static u32 ext2fs_balloc(u32 dev, u32 inum);
static void ext2fs_bfree(int dev, u32 b);
static u32 ext2fs_bmap(struct inode *ip, u32 bn);
static void ext2fs_itrunc(struct inode *ip);
struct ext2fs_addrs ext2fs_addrs[NINODE];
struct ext2_super_block ext2_sb;
u32 first_partition_block = 0;
extern struct mbr mbr;

void ext2fs_readsb(int dev, struct ext2_super_block *sb)
{
    first_partition_block = (mbr.part[0].lba_start / 2);
    const u32 sb_blockno  = first_partition_block + 1; // superblock is at offset 1024 bytes
    struct buf *bp        = bread(dev, sb_blockno);
    memmove(sb, bp->data, sizeof(*sb));
    brelse(bp);
}

// Zero a block.
static void ext2fs_bzero(int dev, int bno)
{
    struct buf *bp = bread(dev, bno);
    memset(bp->data, 0, BSIZE);
    bwrite(bp);
    brelse(bp);
}

// check if a block is free and return its bit number
static u32 ext2fs_get_free_bit(char *bitmap, u32 nbits)
{
    const u32 bytes = (nbits + 7) / 8;
    for (u32 i = 0; i < bytes && i < EXT2_BSIZE; i++) {
        for (u32 j = 0; j < 8; j++) {
            u32 bit = i * 8 + j;
            if (bit >= nbits)
                break;
            const u8 mask = (u8)(1U << j);
            if ((bitmap[i] & mask) != 0)
                continue;
            bitmap[i] |= mask;
            return bit;
        }
    }
    return (u32)-1;
}

// Allocate a zeroed disk block.
static u32 ext2fs_balloc(u32 dev, u32 inum)
{
    struct ext2_group_desc bgdesc;
    u32 desc_blockno = first_partition_block + 2; // block group descriptor table starts at block 2

    int gno         = GET_GROUP_NO(inum, ext2_sb);
    struct buf *bp1 = bread(dev, desc_blockno);
    memmove(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp1);
    struct buf *bp2 = bread(dev, bgdesc.bg_block_bitmap + first_partition_block);

    u32 fbit = ext2fs_get_free_bit((char *)bp2->data, ext2_sb.s_blocks_per_group);
    if (fbit != (u32)-1) {
        bwrite(bp2);
        brelse(bp2);

        u32 group_first_block = ext2_sb.s_first_data_block + gno * ext2_sb.s_blocks_per_group;
        u32 rel_block         = group_first_block + fbit;
        ext2fs_bzero(dev, rel_block + first_partition_block);
        return rel_block;
    }
    brelse(bp2);
    panic("ext2_balloc: out of blocks\n");
}

// Free a disk block.
static void ext2fs_bfree(int dev, u32 b)
{
    struct ext2_group_desc bgdesc;
    u32 desc_blockno = first_partition_block + 2; // block group descriptor table starts at block 2

    if (b < ext2_sb.s_first_data_block) {
        panic("ext2fs_bfree: invalid block\n");
    }

    u32 block_index = b - ext2_sb.s_first_data_block;
    u32 gno         = block_index / ext2_sb.s_blocks_per_group;
    u32 offset      = block_index % ext2_sb.s_blocks_per_group;

    struct buf *bp1 = bread(dev, desc_blockno);
    memmove(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    struct buf *bp2 = bread(dev, bgdesc.bg_block_bitmap + first_partition_block);
    u32 byte_index  = offset / 8;
    if (byte_index >= EXT2_BSIZE) {
        panic("ext2fs_bfree: bitmap overflow\n");
    }
    u8 mask = (u8)(1U << (offset % 8));

    if ((bp2->data[byte_index] & mask) == 0) {
        panic("ext2fs_bfree: block already free\n");
    }
    bp2->data[byte_index] &= ~mask;
    bwrite(bp2);
    brelse(bp2);
    brelse(bp1);
}

void ext2fs_iinit(int dev)
{
    mbr_load();
    ext2fs_readsb(dev, &ext2_sb);
    const u32 block_bytes   = 1024u << ext2_sb.s_log_block_size;
    const u64 partition_mb  = ((u64)ext2_sb.s_blocks_count * block_bytes) / (1024ull * 1024ull);
    const u64 size_value    = (partition_mb >= 1024ull) ? partition_mb / 1024ull : partition_mb;
    const char *size_suffix = (partition_mb >= 1024ull) ? "GB" : "MB";
    boot_message(WARNING_LEVEL_INFO,
                 "ext2: size: %llu %s, block_size: %u, block_count: %u, inodes: %u",
                 (unsigned long long)size_value,
                 size_suffix,
                 block_bytes,
                 ext2_sb.s_blocks_count,
                 ext2_sb.s_inodes_count);
}

struct inode *ext2fs_ialloc(u32 dev, short type)
{
    struct ext2_group_desc bgdesc;
    u32 desc_blockno = first_partition_block + 2; // block group descriptor table starts at block 2

    int bgcount = ext2_sb.s_blocks_count / ext2_sb.s_blocks_per_group;
    for (int i = 0; i <= bgcount; i++) {
        struct buf *group_desc_buf = bread(dev, desc_blockno);
        memmove(&bgdesc, group_desc_buf->data + i * sizeof(bgdesc), sizeof(bgdesc));
        brelse(group_desc_buf);

        struct buf *ibitmap_buff = bread(dev, bgdesc.bg_inode_bitmap + first_partition_block);
        u32 fbit                 = ext2fs_get_free_bit((char *)ibitmap_buff->data, ext2_sb.s_inodes_per_group);
        if (fbit == (u32)-1) {
            brelse(ibitmap_buff);
            continue;
        }

        int inodes_per_block = EXT2_BSIZE / ext2_sb.s_inode_size;
        if (inodes_per_block == 0) {
            panic("ext2fs_ialloc: invalid inode size");
        }

        int bno                 = bgdesc.bg_inode_table + fbit / inodes_per_block + first_partition_block;
        int iindex              = fbit % inodes_per_block;
        struct buf *dinode_buff = bread(dev, bno);
        u8 *slot                = dinode_buff->data + (iindex * ext2_sb.s_inode_size);

        memset(slot, 0, ext2_sb.s_inode_size);
        auto din = (struct ext2_inode *)slot;
        if (type == T_DIR) {
            din->i_mode = S_IFDIR;
        } else if (type == T_FILE) {
            din->i_mode = S_IFREG;
        } else if (type == T_DEV) {
            din->i_mode = S_IFCHR;
        }
        bwrite(dinode_buff);
        bwrite(ibitmap_buff);
        brelse(dinode_buff);
        brelse(ibitmap_buff);

        int inum = i * ext2_sb.s_inodes_per_group + fbit + 1;
        return iget(dev, inum);
    }
    panic("ext2_ialloc: no inodes");
}

void ext2fs_iupdate(struct inode *ip)
{
    struct ext2_group_desc bgdesc;
    u32 desc_blockno = first_partition_block + 2; // block group descriptor table starts at block 2

    int gno        = GET_GROUP_NO(ip->inum, ext2_sb);
    int ioff       = GET_INODE_INDEX(ip->inum, ext2_sb);
    struct buf *bp = bread(ip->dev, desc_blockno);
    memmove(&bgdesc, bp->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp);
    int bno         = bgdesc.bg_inode_table + ioff / (EXT2_BSIZE / ext2_sb.s_inode_size) + first_partition_block;
    int iindex      = ioff % (EXT2_BSIZE / ext2_sb.s_inode_size);
    struct buf *bp1 = bread(ip->dev, bno);
    if (ext2_sb.s_inode_size > EXT2_MAX_INODE_SIZE) {
        panic("ext2fs_iupdate: inode too large");
    }

    u8 raw[EXT2_MAX_INODE_SIZE];
    memmove(raw, bp1->data + iindex * ext2_sb.s_inode_size, ext2_sb.s_inode_size);
    auto din = (struct ext2_inode *)raw;

    if (ip->type == T_DIR) {
        din->i_mode = S_IFDIR;
    }
    if (ip->type == T_FILE) {
        din->i_mode = S_IFREG;
    }
    if (ip->type == T_DEV) {
        din->i_mode = S_IFCHR;
    }
    din->i_links_count = ip->nlink;
    din->i_size        = ip->size;
    din->i_dtime       = 0;
    din->i_faddr       = 0;
    din->i_file_acl    = 0;
    din->i_flags       = 0;
    din->i_generation  = 0;
    din->i_gid         = 0;
    din->i_mtime       = 0;
    din->i_uid         = 0;
    din->i_atime       = 0;

    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;
    memmove(din->i_block, ad->addrs, sizeof(ad->addrs));
    memmove(bp1->data + (iindex * ext2_sb.s_inode_size), raw, ext2_sb.s_inode_size);
    bwrite(bp1);
    brelse(bp1);
}

void ext2fs_ilock(struct inode *ip)
{
    struct ext2_group_desc bgdesc;
    if (ip == nullptr || ip->ref < 1) {
        panic("ext2fs_ilock");
    }

    ASSERT(ip->addrs != nullptr, "ip->addrs is null in ext2fs_ilock before lock");
    acquiresleep(&ip->lock);
    ASSERT(ip->addrs != nullptr, "ip->addrs is null in ext2fs_ilock");
    const auto ad  = (struct ext2fs_addrs *)ip->addrs;
    u32 desc_block = first_partition_block + 2; // Group descriptor at ext2 block 2 = sector 4

    if (ip->valid == 0) {
        const int gno  = GET_GROUP_NO(ip->inum, ext2_sb);
        const int ioff = GET_INODE_INDEX(ip->inum, ext2_sb);
        struct buf *bp = bread(ip->dev, desc_block);
        memmove(&bgdesc, bp->data + gno * sizeof(bgdesc), sizeof(bgdesc));
        brelse(bp);
        const int bno    = bgdesc.bg_inode_table + ioff / (EXT2_BSIZE / ext2_sb.s_inode_size) + first_partition_block;
        const int iindex = ioff % (EXT2_BSIZE / ext2_sb.s_inode_size);
        struct buf *bp1  = bread(ip->dev, bno);
        if (ext2_sb.s_inode_size > EXT2_MAX_INODE_SIZE)
            panic("ext2fs_ilock: inode too large");
        u8 raw[EXT2_MAX_INODE_SIZE];
        memmove(raw, bp1->data + iindex * ext2_sb.s_inode_size, ext2_sb.s_inode_size);
        brelse(bp1);

        struct ext2_inode *din = (struct ext2_inode *)raw;

        if (S_ISDIR(din->i_mode) || din->i_mode == T_DIR) {
            ip->type = T_DIR;
        } else if (S_ISREG(din->i_mode)) {
            ip->type = T_FILE;
        } else if (S_ISCHR(din->i_mode)) {
            ip->type = T_DEV;
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
        ip->iops  = &ext2fs_inode_ops;
        memmove(ad->addrs, din->i_block, sizeof(ad->addrs));

        ip->valid = 1;
        if (ip->type == 0) {
            panic("ext2fs_ilock: no type");
        }
    }
}

void ext2fs_iunlock(struct inode *ip)
{
    if (ip == nullptr || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("ext2fs_iunlock");

    releasesleep(&ip->lock);
}

// Free an inode
static void ext2fs_ifree(struct inode *ip)
{
    struct ext2_group_desc bgdesc;
    u32 desc_blockno = first_partition_block + 2; // block group descriptor table starts at block 2

    int gno         = GET_GROUP_NO(ip->inum, ext2_sb);
    struct buf *bp1 = bread(ip->dev, desc_blockno);
    memmove(&bgdesc, bp1->data + gno * sizeof(bgdesc), sizeof(bgdesc));
    brelse(bp1);
    struct buf *bp2 = bread(ip->dev, bgdesc.bg_inode_bitmap + first_partition_block);
    u32 index       = (ip->inum - 1) % ext2_sb.s_inodes_per_group;
    u32 byte_index  = index / 8;
    if (byte_index >= EXT2_BSIZE) {
        panic("ext2fs_ifree: bitmap overflow\n");
    }
    u8 mask = (u8)(1U << (index % 8));

    if ((bp2->data[byte_index] & mask) == 0) {
        panic("ext2fs_ifree: inode already free (inum=%u type=%d nlink=%d ref=%d)\n",
              ip->inum,
              ip->type,
              ip->nlink,
              ip->ref);
    }
    bp2->data[byte_index] &= ~mask;
    bwrite(bp2);
    brelse(bp2);
}

void ext2fs_iput(struct inode *ip)
{
    acquiresleep(&ip->lock);
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    if (ip->valid && ip->nlink == 0) {
        acquire(&icache.lock);
        int r = ip->ref;
        release(&icache.lock);
        if (r == 1) {
            // inode has no links and no other references: truncate and free.
            ext2fs_ifree(ip);
            ext2fs_itrunc(ip);
            ip->type = 0;
            ip->iops->iupdate(ip);
            ip->valid = 0;
        }
    }
    releasesleep(&ip->lock);

    acquire(&icache.lock);
    ip->ref--;
    if (ip->ref == 0) {
        ad->busy  = 0;
        ip->iops  = nullptr;
        ip->addrs = nullptr;
    }
    release(&icache.lock);
}

void ext2fs_iunlockput(struct inode *ip)
{
    ip->iops->iunlock(ip);
    ip->iops->iput(ip);
}

void ext2fs_stati(struct inode *ip, struct stat *st)
{
    st->dev     = (int)ip->dev;
    st->ino     = ip->inum;
    st->type    = ip->type;
    st->nlink   = ip->nlink;
    st->size    = ip->size;
    st->ref     = ip->ref;
    st->i_atime = ip->i_atime;
    st->i_ctime = ip->i_ctime;
    st->i_mtime = ip->i_mtime;
    st->i_dtime = ip->i_dtime;
    st->i_uid   = ip->i_uid;
    st->i_gid   = ip->i_gid;
    st->i_flags = ip->i_flags;
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
/*
 * EXT2BSIZE -> 1024
 * If < EXT2_NDIR_BLOCKS then it is directly mapped, allocate and return
 * If < 128 (Indirect blocks) then need to allocate using indirect block
 * If < 128*128 (Double indirect) ...
 * If < 128*128*128 (Triple indirect) ...
 * Else panic()
*/
static u32 ext2fs_bmap(struct inode *ip, u32 bn)
{
    u32 addr, *a, *b;
    struct buf *bp, *bp1;
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    if (bn < EXT2_NDIR_BLOCKS) {
        if ((addr = ad->addrs[bn]) == 0) {
            addr          = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[bn] = addr;
        }
        return addr + first_partition_block;
    }
    bn -= EXT2_NDIR_BLOCKS;
    if (bn < EXT2_INDIRECT) {
        if ((addr = ad->addrs[EXT2_IND_BLOCK]) == 0) {
            addr                      = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[EXT2_IND_BLOCK] = addr;
        }
        bp        = bread(ip->dev, first_partition_block + addr);
        a         = (u32 *)bp->data;
        u32 entry = a[bn];
        if (entry == 0) {
            entry = ext2fs_balloc(ip->dev, ip->inum);
            a[bn] = entry;
            bwrite(bp);
        }
        brelse(bp);
        return entry + first_partition_block;
    }
    bn -= EXT2_INDIRECT;

    if (bn < EXT2_DINDIRECT) {
        if ((addr = ad->addrs[EXT2_DIND_BLOCK]) == 0) {
            addr                       = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[EXT2_DIND_BLOCK] = addr;
        }
        bp              = bread(ip->dev, first_partition_block + addr);
        a               = (u32 *)bp->data;
        u32 first_index = bn / EXT2_INDIRECT;
        u32 entry       = a[first_index];
        if (entry == 0) {
            entry          = ext2fs_balloc(ip->dev, ip->inum);
            a[first_index] = entry;
            bwrite(bp);
        }
        brelse(bp);

        bp1              = bread(ip->dev, first_partition_block + entry);
        b                = (u32 *)bp1->data;
        u32 second_index = bn % EXT2_INDIRECT;
        u32 leaf         = b[second_index];
        if (leaf == 0) {
            leaf            = ext2fs_balloc(ip->dev, ip->inum);
            b[second_index] = leaf;
            bwrite(bp1);
        }
        brelse(bp1);
        return leaf + first_partition_block;
    }
    bn -= EXT2_DINDIRECT;

    if (bn < EXT2_TINDIRECT) {
        if ((addr = ad->addrs[EXT2_TIND_BLOCK]) == 0) {
            addr                       = ext2fs_balloc(ip->dev, ip->inum);
            ad->addrs[EXT2_TIND_BLOCK] = addr;
        }
        bp              = bread(ip->dev, first_partition_block + addr);
        a               = (u32 *)bp->data;
        u32 first_index = bn / EXT2_DINDIRECT;
        u32 entry       = a[first_index];
        if (entry == 0) {
            entry          = ext2fs_balloc(ip->dev, ip->inum);
            a[first_index] = entry;
            bwrite(bp);
        }
        brelse(bp);

        bp1            = bread(ip->dev, first_partition_block + entry);
        b              = (u32 *)bp1->data;
        u32 remainder  = bn % EXT2_DINDIRECT;
        u32 second_idx = remainder / EXT2_INDIRECT;
        u32 mid        = b[second_idx];
        if (mid == 0) {
            mid           = ext2fs_balloc(ip->dev, ip->inum);
            b[second_idx] = mid;
            bwrite(bp1);
        }
        brelse(bp1);

        struct buf *bp2 = bread(ip->dev, first_partition_block + mid);
        u32 *c          = (u32 *)bp2->data;
        u32 third_idx   = remainder % EXT2_INDIRECT;
        u32 leaf        = c[third_idx];
        if (leaf == 0) {
            leaf         = ext2fs_balloc(ip->dev, ip->inum);
            c[third_idx] = leaf;
            bwrite(bp2);
        }
        brelse(bp2);
        return leaf + first_partition_block;
    }
    panic("ext2_bmap: block number out of range\n");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void ext2fs_itrunc(struct inode *ip)
{
    u32 i, j;
    struct buf *bp1, *bp2;
    u32 *a, *b;
    struct ext2fs_addrs *ad = (struct ext2fs_addrs *)ip->addrs;

    // for direct blocks
    for (i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (ad->addrs[i]) {
            ext2fs_bfree(ip->dev, ad->addrs[i]);
            ad->addrs[i] = 0;
        }
    }
    // EXT2_INDIRECT -> (EXT2_BSIZE / sizeof(u32))
    // for indirect blocks
    if (ad->addrs[EXT2_IND_BLOCK]) {
        bp1 = bread(ip->dev, ad->addrs[EXT2_IND_BLOCK] + first_partition_block);
        a   = (u32 *)bp1->data;
        for (i = 0; i < EXT2_INDIRECT; i++) {
            if (a[i]) {
                ext2fs_bfree(ip->dev, a[i]);
                a[i] = 0;
            }
        }
        brelse(bp1);
        ext2fs_bfree(ip->dev, ad->addrs[EXT2_IND_BLOCK]);
        ad->addrs[EXT2_IND_BLOCK] = 0;
    }

    // for double indirect blocks
    if (ad->addrs[EXT2_DIND_BLOCK]) {
        bp1 = bread(ip->dev, ad->addrs[EXT2_DIND_BLOCK] + first_partition_block);
        a   = (u32 *)bp1->data;
        for (i = 0; i < EXT2_INDIRECT; i++) {
            if (a[i]) {
                bp2 = bread(ip->dev, a[i] + first_partition_block);
                b   = (u32 *)bp2->data;
                for (j = 0; j < EXT2_INDIRECT; j++) {
                    if (b[j]) {
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
    if (ad->addrs[EXT2_TIND_BLOCK]) {
        bp1 = bread(ip->dev, ad->addrs[EXT2_TIND_BLOCK] + first_partition_block);
        a   = (u32 *)bp1->data;
        for (i = 0; i < EXT2_INDIRECT; i++) {
            if (a[i]) {
                bp2 = bread(ip->dev, a[i] + first_partition_block);
                b   = (u32 *)bp2->data;
                for (j = 0; j < EXT2_INDIRECT; j++) {
                    if (b[j]) {
                        struct buf *bp3 = bread(ip->dev, b[j] + first_partition_block);
                        u32 *c          = (u32 *)bp3->data;
                        for (u32 k = 0; k < EXT2_INDIRECT; k++) {
                            if (c[k]) {
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
    ip->iops->iupdate(ip);
}


int ext2fs_readi(struct inode *ip, char *dst, u32 off, u32 n)
{
    u32 m;

    if (ip->type == T_DEV) {
        int major = devtab_lookup_major(ip);
        if (major < 0 || major >= NDEV || !devsw[major].read) {
            return -1;
        }
        return devsw[major].read(ip, dst, n, off);
    }

    if (off > ip->size || off + n < off) {
        return -1;
    }
    if (off + n > ip->size) {
        n = ip->size - off;
    }

    for (u32 tot = 0; tot < n; tot += m, off += m, dst += m) {
        u32 block      = ext2fs_bmap(ip, off / EXT2_BSIZE);
        struct buf *bp = bread(ip->dev, block);
        m              = min(n - tot, EXT2_BSIZE - off % EXT2_BSIZE);
        memmove(dst, bp->data + off % EXT2_BSIZE, m);
        brelse(bp);
    }
    return n;
}

int ext2fs_writei(struct inode *ip, char *src, u32 off, u32 n)
{
    u32 m;

    if (ip->type == T_DEV) {
        int major = devtab_lookup_major(ip);
        if (major < 0 || major >= NDEV || !devsw[major].write) {
            return -1;
        }
        return devsw[major].write(ip, src, n, off);
    }

    if (off > ip->size || off + n < off) {
        return -1;
    }
    if (off + n > EXT2_MAXFILE * EXT2_BSIZE) {
        return -1;
    }

    for (u32 tot = 0; tot < n; tot += m, off += m, src += m) {
        u32 block      = ext2fs_bmap(ip, off / EXT2_BSIZE);
        struct buf *bp = bread(ip->dev, block);
        m              = min(n - tot, EXT2_BSIZE - off%EXT2_BSIZE);
        memmove(bp->data + off % EXT2_BSIZE, src, m);
        bwrite(bp);
        brelse(bp);
    }

    if (n > 0 && off > ip->size) {
        ip->size = off;
        ip->iops->iupdate(ip);
    }
    return n;
}

int ext2fs_namecmp(const char *s, const char *t)
{
    return strncmp(s, t, EXT2_NAME_LEN);
}

static inline u16 ext2_dirent_size(u8 name_len)
{
    u16 size = 8 + name_len;
    return (size + 3) & ~3;
}

struct inode *ext2fs_dirlookup(struct inode *dp, char *name, u32 *poff)
{
    struct ext2_dir_entry_2 de;
    char file_name[EXT2_NAME_LEN + 1];
    for (u32 off = 0; off < dp->size;) {
        memset(&de, 0, sizeof(de));
        if (dp->iops->readi(dp, (char *)&de, off, 8) != 8) {
            panic("ext2fs_dirlookup: header read");
        }
        if (de.rec_len < 8 || de.rec_len > EXT2_BSIZE) {
            panic("ext2fs_dirlookup: bad rec_len");
        }
        if (de.name_len > 0) {
            int to_copy = de.name_len;
            if (to_copy > EXT2_NAME_LEN) {
                to_copy = EXT2_NAME_LEN;
            }
            if (dp->iops->readi(dp, (char *)de.name, off + 8, to_copy) != to_copy) {
                panic("ext2fs_dirlookup: name read");
            }
        }
        if (de.inode == 0) {
            off += de.rec_len;
            continue;
        }
        if (de.name_len > EXT2_NAME_LEN)
            panic("ext2fs_dirlookup: name too long");
        memmove(file_name, de.name, de.name_len);
        file_name[de.name_len] = '\0';
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

int ext2fs_dirlink(struct inode *dp, char *name, u32 inum)
{
    if (name == nullptr) {
        return -1;
    }

    int name_len = strlen(name);
    if (name_len <= 0 || name_len > EXT2_NAME_LEN) {
        return -1;
    }

    struct ext2_dir_entry_2 de;
    struct inode *ip;

    if ((ip = dp->iops->dirlookup(dp, name, nullptr)) != nullptr) {
        ip->iops->iput(ip);
        return -1;
    }

    u32 off     = dp->size;
    u16 rec_len = ext2_dirent_size((u8)name_len);

    memset(&de, 0, sizeof(de));
    de.inode     = inum;
    de.rec_len   = rec_len;
    de.name_len  = name_len;
    de.file_type = EXT2_FT_UNKNOWN;
    memmove(de.name, name, name_len);

    dp->iops->writei(dp, (char *)&de, off, rec_len);

    return 0;
}