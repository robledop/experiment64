#pragma once

#include <stdint.h>
#include "vfs.h"

extern struct ext2_inode_operations ext2fs_inode_ops;
extern struct icache icache;

// Block size for ext2
#define EXT2_BSIZE 1024

#define EXT2_MAX_INODE_SIZE EXT2_BSIZE

#define GET_GROUP_NO(inum, ext2_sb) (((inum) - 1) / ((ext2_sb).s_inodes_per_group))
#define GET_INODE_INDEX(inum, ext2_sb) (((inum) - 1) % ((ext2_sb).s_inodes_per_group))

/*
 * Constants relative to the data blocks
 */
#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS (EXT2_TIND_BLOCK + 1)

// Block sizes
#define EXT2_INDIRECT (EXT2_BSIZE / sizeof(uint32_t))
#define EXT2_DINDIRECT ((EXT2_BSIZE / sizeof(uint32_t)) * EXT2_INDIRECT)
#define EXT2_TINDIRECT ((EXT2_BSIZE / sizeof(uint32_t)) * EXT2_DINDIRECT)
#define EXT2_MAXFILE (EXT2_NDIR_BLOCKS + EXT2_INDIRECT + EXT2_DINDIRECT + EXT2_TINDIRECT)

// for directory entry
#define EXT2_NAME_LEN 255

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_FT_CHRDEV 3
#define EXT2_FT_BLKDEV 4
#define EXT2_FT_FIFO 5
#define EXT2_FT_SOCK 6
#define EXT2_FT_SYMLINK 7

#define NINODE 50

struct ext2fs_addrs
{
    uint32_t busy;
    uint32_t addrs[EXT2_N_BLOCKS];
};

extern struct ext2fs_addrs ext2fs_addrs[NINODE];

struct ext2_super_block
{
    uint32_t s_inodes_count; /* Inodes count */
    uint32_t s_blocks_count; /* Blocks count */
    uint32_t s_r_blocks_count; /* Reserved blocks count */
    uint32_t s_free_blocks_count; /* Free blocks count */
    uint32_t s_free_inodes_count; /* Free inodes count */
    uint32_t s_first_data_block; /* First Data Block */
    uint32_t s_log_block_size; /* Block size */
    uint32_t s_log_frag_size; /* Fragment size */
    uint32_t s_blocks_per_group; /* # Blocks per group */
    uint32_t s_frags_per_group; /* # Fragments per group */
    uint32_t s_inodes_per_group; /* # Inodes per group */
    uint32_t s_mtime; /* Mount time */
    uint32_t s_wtime; /* Write time */
    uint16_t s_mnt_count; /* Mount count */
    uint16_t s_max_mnt_count; /* Maximal mount count */
    uint16_t s_magic; /* Magic signature */
    uint16_t s_state; /* File system state */
    uint16_t s_errors; /* Behaviour when detecting errors */
    uint16_t s_minor_rev_level; /* minor revision level */
    uint32_t s_lastcheck; /* time of last check */
    uint32_t s_checkinterval; /* max. time between checks */
    uint32_t s_creator_os; /* OS */
    uint32_t s_rev_level; /* Revision level */
    uint16_t s_def_resuid; /* Default uid for reserved blocks */
    uint16_t s_def_resgid; /* Default gid for reserved blocks */
    /*
     * These fields are for EXT2_DYNAMIC_REV superblocks only.
     *
     * Note: the difference between the compatible feature set and
     * the incompatible feature set is that if there is a bit set
     * in the incompatible feature set that the kernel doesn't
     * know about, it should refuse to mount the filesystem.
     *
     * e2fsck's requirements are stricter; if it doesn't know
     * about a feature in either the compatible or incompatible
     * feature set, it must abort and not try to meddle with
     * things it doesn't understand...
     */
    uint32_t s_first_ino; /* First non-reserved inode */
    uint16_t s_inode_size; /* size of inode structure */
    uint16_t s_block_group_nr; /* block group # of this superblock */
    uint32_t s_feature_compat; /* compatible feature set */
    uint32_t s_feature_incompat; /* incompatible feature set */
    uint32_t s_feature_ro_compat; /* readonly-compatible feature set */
    uint8_t s_uuid[16]; /* 128-bit uuid for volume */
    char s_volume_name[16]; /* volume name */
    char s_last_mounted[64]; /* directory where last mounted */
    uint32_t s_algorithm_usage_bitmap; /* For compression */
    /*
     * Performance hints.  Directory preallocation should only
     * happen if the EXT2_COMPAT_PREALLOC flag is on.
     */
    uint8_t s_prealloc_blocks; /* Nr of blocks to try to preallocate*/
    uint8_t s_prealloc_dir_blocks; /* Nr to preallocate for dirs */
    uint16_t s_padding1;
    /*
     * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
     */
    uint8_t s_journal_uuid[16]; /* uuid of journal superblock */
    uint32_t s_journal_inum; /* inode number of journal file */
    uint32_t s_journal_dev; /* device number of journal file */
    uint32_t s_last_orphan; /* start of the list of inodes to delete */
    uint32_t s_hash_seed[4]; /* HTREE hash seed */
    uint8_t s_def_hash_version; /* Default hash version to use */
    uint8_t s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg; /* First metablock block group */
    uint32_t s_reserved[190]; /* Padding to the end of the block */
};

struct ext2_group_desc
{
    uint32_t bg_block_bitmap; /* Blocks bitmap block */
    uint32_t bg_inode_bitmap; /* Inodes bitmap block */
    uint32_t bg_inode_table; /* Inodes table block */
    uint16_t bg_free_blocks_count; /* Free blocks count */
    uint16_t bg_free_inodes_count; /* Free inodes count */
    uint16_t bg_used_dirs_count; /* Directories count */
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};

/*
 * Structure of an inode on the disk
 */
struct ext2_disk_inode
{
    uint16_t i_mode; /* File mode */
    uint16_t i_uid; /* Low 16 bits of Owner Uid */
    uint32_t i_size; /* Size in bytes */
    uint32_t i_atime; /* Access time */
    uint32_t i_ctime; /* Creation time */
    uint32_t i_mtime; /* Modification time */
    uint32_t i_dtime; /* Deletion Time */
    uint16_t i_gid; /* Low 16 bits of Group Id */
    uint16_t i_links_count; /* Links count */
    uint32_t i_blocks; /* Blocks count */
    uint32_t i_flags; /* File flags */
    union
    {
        struct
        {
            uint32_t l_i_reserved1;
        } linux1;

        struct
        {
            uint32_t h_i_translator;
        } hurd1;

        struct
        {
            uint32_t m_i_reserved1;
        } masix1;
    } osd1; /* OS dependent 1 */
    uint32_t i_block[EXT2_N_BLOCKS]; /* Pointers to blocks */
    uint32_t i_generation; /* File version (for NFS) */
    uint32_t i_file_acl; /* File ACL */
    uint32_t i_dir_acl; /* Directory ACL */
    uint32_t i_faddr; /* Fragment address */
    union
    {
        struct
        {
            uint16_t l_i_frag; /* Fragment number */
            uint16_t l_i_fsize; /* Fragment size */
            uint16_t i_pad1;
            uint16_t l_i_uid_high; /* these 2 fields    */
            uint16_t l_i_gid_high; /* were reserved2[0] */
            uint32_t l_i_reserved2;
        } linux2;

        struct
        {
            uint8_t h_i_frag; /* Fragment number */
            uint8_t h_i_fsize; /* Fragment size */
            uint16_t h_i_mode_high;
            uint16_t h_i_uid_high;
            uint16_t h_i_gid_high;
            uint32_t h_i_author;
        } hurd2;

        struct
        {
            uint16_t m_i_frag; /* Fragment number */
            uint16_t m_i_fsize; /* Fragment size */
            uint16_t m_pad1;
            uint32_t m_i_reserved2[2];
        } masix2;
    } osd2; /* OS dependent 2 */
} __attribute__((packed));

/*
 * The new version of the directory entry.  Since EXT2 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct ext2_dir_entry_2
{
    uint32_t inode; /* Inode number */
    uint16_t rec_len; /* Directory entry length */
    uint8_t name_len; /* Name length */
    uint8_t file_type;
    char name[EXT2_NAME_LEN]; /* File name */
};

// file type
#define S_IFMT 00170000  // type of file
#define S_IFSOCK 0140000 // socket
#define S_IFLNK 0120000  // symbolic link
#define S_IFREG 0100000  // regular file
#define S_IFBLK 0060000  // block device
#define S_IFDIR 0040000  // directory
#define S_IFCHR 0020000  // character device
#define S_IFIFO 0010000  // fifo
#define S_ISUID 0004000  // set user id on execution
#define S_ISGID 0002000  // set group id on execution
#define S_ISVTX 0001000  // sticky bit

#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#include "sleeplock.h"

// In-memory inode structure
struct ext2_inode
{
    uint32_t dev;
    uint32_t inum;
    int ref;
    sleeplock_t lock;
    int valid;

    uint16_t type;
    uint16_t major;
    uint16_t minor;
    uint16_t nlink;
    uint32_t size;
    struct ext2fs_addrs* addrs;

    // Additional fields needed for stat
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_flags;
};

struct icache
{
    spinlock_t lock;
    struct ext2_inode inode[NINODE];
};

void ext2fs_readsb(int dev, struct ext2_super_block* sb);
int ext2fs_dirlink(struct ext2_inode*, const char*, uint32_t);
struct ext2_inode* ext2fs_dirlookup(const struct ext2_inode*, const char*, uint32_t*);
struct ext2_inode* ext2fs_ialloc(uint32_t, short);
void ext2_init_inode(int dev);
void ext2fs_ilock(struct ext2_inode*);
void ext2fs_iput(struct ext2_inode*);
void ext2fs_iunlock(struct ext2_inode*);
void ext2fs_iunlockput(struct ext2_inode*);
void ext2fs_iupdate(const struct ext2_inode*);
int ext2_read_inode(const struct ext2_inode*, char*, uint32_t, uint32_t);
void ext2_stat_inode(const struct ext2_inode*, struct stat*);
int ext2_write_inode(struct ext2_inode*, const char*, uint32_t, uint32_t);

vfs_inode_t* ext2_mount(uint8_t drive_index, uint32_t partition_lba);
