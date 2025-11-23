#pragma once

#include "types.h"
#include "param.h"
#include "file.h"
#include "stat.h"

extern struct inode_operations ext2fs_inode_ops;
extern struct icache icache;

// Block size for ext2
#define EXT2_BSIZE 1024

#define EXT2_MAX_INODE_SIZE EXT2_BSIZE

#define GET_GROUP_NO(inum, ext2_sb) 	((inum - 1) / ext2_sb.s_inodes_per_group)
#define GET_INODE_INDEX(inum, ext2_sb) 	((inum - 1) % ext2_sb.s_inodes_per_group)

/*
 * Constants relative to the data blocks
 */
#define	EXT2_NDIR_BLOCKS		12
#define	EXT2_IND_BLOCK			EXT2_NDIR_BLOCKS
#define	EXT2_DIND_BLOCK			(EXT2_IND_BLOCK + 1)
#define	EXT2_TIND_BLOCK			(EXT2_DIND_BLOCK + 1)
#define	EXT2_N_BLOCKS			(EXT2_TIND_BLOCK + 1)

// Block sizes
#define EXT2_INDIRECT                   (EXT2_BSIZE / sizeof(u32))
#define EXT2_DINDIRECT                  (EXT2_BSIZE / sizeof(u32))*EXT2_INDIRECT
#define EXT2_TINDIRECT                  (EXT2_BSIZE / sizeof(u32))*EXT2_DINDIRECT
#define EXT2_MAXFILE                    (EXT2_NDIR_BLOCKS + EXT2_INDIRECT + EXT2_DINDIRECT + EXT2_TINDIRECT)

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

struct ext2fs_addrs
{
    u32 busy;
    u32 addrs[EXT2_N_BLOCKS];
};

extern struct ext2fs_addrs ext2fs_addrs[NINODE];

struct ext2_super_block
{
    u32 s_inodes_count;      /* Inodes count */
    u32 s_blocks_count;      /* Blocks count */
    u32 s_r_blocks_count;    /* Reserved blocks count */
    u32 s_free_blocks_count; /* Free blocks count */
    u32 s_free_inodes_count; /* Free inodes count */
    u32 s_first_data_block;  /* First Data Block */
    u32 s_log_block_size;    /* Block size */
    u32 s_log_frag_size;     /* Fragment size */
    u32 s_blocks_per_group;  /* # Blocks per group */
    u32 s_frags_per_group;   /* # Fragments per group */
    u32 s_inodes_per_group;  /* # Inodes per group */
    u32 s_mtime;             /* Mount time */
    u32 s_wtime;             /* Write time */
    u16 s_mnt_count;         /* Mount count */
    u16 s_max_mnt_count;     /* Maximal mount count */
    u16 s_magic;             /* Magic signature */
    u16 s_state;             /* File system state */
    u16 s_errors;            /* Behaviour when detecting errors */
    u16 s_minor_rev_level;   /* minor revision level */
    u32 s_lastcheck;         /* time of last check */
    u32 s_checkinterval;     /* max. time between checks */
    u32 s_creator_os;        /* OS */
    u32 s_rev_level;         /* Revision level */
    u16 s_def_resuid;        /* Default uid for reserved blocks */
    u16 s_def_resgid;        /* Default gid for reserved blocks */
    /*
     * These fields are for EXT2_DYNAMIC_REV superblocks only.
     *
     * Note: the difference between the compatible feature set and
     * the incompatible feature set is that if there is a bit set
     * in the incompatible feature set that the kernel doesn't
     * know about, it should refuse to mount the filesystem.
     *
     * e2fsck's requirements are more strict; if it doesn't know
     * about a feature in either the compatible or incompatible
     * feature set, it must abort and not try to meddle with
     * things it doesn't understand...
     */
    u32 s_first_ino;              /* First non-reserved inode */
    u16 s_inode_size;             /* size of inode structure */
    u16 s_block_group_nr;         /* block group # of this superblock */
    u32 s_feature_compat;         /* compatible feature set */
    u32 s_feature_incompat;       /* incompatible feature set */
    u32 s_feature_ro_compat;      /* readonly-compatible feature set */
    u8 s_uuid[16];                /* 128-bit uuid for volume */
    char s_volume_name[16];       /* volume name */
    char s_last_mounted[64];      /* directory where last mounted */
    u32 s_algorithm_usage_bitmap; /* For compression */
    /*
     * Performance hints.  Directory preallocation should only
     * happen if the EXT2_COMPAT_PREALLOC flag is on.
     */
    u8 s_prealloc_blocks;     /* Nr of blocks to try to preallocate*/
    u8 s_prealloc_dir_blocks; /* Nr to preallocate for dirs */
    u16 s_padding1;
    /*
     * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
     */
    u8 s_journal_uuid[16]; /* uuid of journal superblock */
    u32 s_journal_inum;    /* inode number of journal file */
    u32 s_journal_dev;     /* device number of journal file */
    u32 s_last_orphan;     /* start of list of inodes to delete */
    u32 s_hash_seed[4];    /* HTREE hash seed */
    u8 s_def_hash_version; /* Default hash version to use */
    u8 s_reserved_char_pad;
    u16 s_reserved_word_pad;
    u32 s_default_mount_opts;
    u32 s_first_meta_bg; /* First metablock block group */
    u32 s_reserved[190]; /* Padding to the end of the block */
};

struct ext2_group_desc
{
    u32 bg_block_bitmap;      /* Blocks bitmap block */
    u32 bg_inode_bitmap;      /* Inodes bitmap block */
    u32 bg_inode_table;       /* Inodes table block */
    u16 bg_free_blocks_count; /* Free blocks count */
    u16 bg_free_inodes_count; /* Free inodes count */
    u16 bg_used_dirs_count;   /* Directories count */
    u16 bg_pad;
    u32 bg_reserved[3];
};

/*
 * Structure of an inode on the disk
 */
struct ext2_inode
{
    u16 i_mode;        /* File mode */
    u16 i_uid;         /* Low 16 bits of Owner Uid */
    u32 i_size;        /* Size in bytes */
    u32 i_atime;       /* Access time */
    u32 i_ctime;       /* Creation time */
    u32 i_mtime;       /* Modification time */
    u32 i_dtime;       /* Deletion Time */
    u16 i_gid;         /* Low 16 bits of Group Id */
    u16 i_links_count; /* Links count */
    u32 i_blocks;      /* Blocks count */
    u32 i_flags;       /* File flags */
    union
    {
        struct
        {
            u32 l_i_reserved1;
        } linux1;

        struct
        {
            u32 h_i_translator;
        } hurd1;

        struct
        {
            u32 m_i_reserved1;
        } masix1;
    } osd1;                     /* OS dependent 1 */
    u32 i_block[EXT2_N_BLOCKS]; /* Pointers to blocks */
    u32 i_generation;           /* File version (for NFS) */
    u32 i_file_acl;             /* File ACL */
    u32 i_dir_acl;              /* Directory ACL */
    u32 i_faddr;                /* Fragment address */
    union
    {
        struct
        {
            u16 l_i_frag;  /* Fragment number */
            u16 l_i_fsize; /* Fragment size */
            u16 i_pad1;
            u16 l_i_uid_high; /* these 2 fields    */
            u16 l_i_gid_high; /* were reserved2[0] */
            u32 l_i_reserved2;
        } linux2;

        struct
        {
            u8 h_i_frag;  /* Fragment number */
            u8 h_i_fsize; /* Fragment size */
            u16 h_i_mode_high;
            u16 h_i_uid_high;
            u16 h_i_gid_high;
            u32 h_i_author;
        } hurd2;

        struct
        {
            u16 m_i_frag;  /* Fragment number */
            u16 m_i_fsize; /* Fragment size */
            u16 m_pad1;
            u32 m_i_reserved2[2];
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
    u32 inode;   /* Inode number */
    u16 rec_len; /* Directory entry length */
    u8 name_len; /* Name length */
    u8 file_type;
    char name[EXT2_NAME_LEN]; /* File name */
};

// file type
#define S_IFMT  00170000 // type of file
#define S_IFSOCK 0140000 // socket
#define S_IFLNK	 0120000 // symbolic link
#define S_IFREG  0100000 // regular file
#define S_IFBLK  0060000 // block device
#define S_IFDIR  0040000 // directory
#define S_IFCHR  0020000 // character device
#define S_IFIFO  0010000 // fifo
#define S_ISUID  0004000 // set user id on execution
#define S_ISGID  0002000 // set group id on execution
#define S_ISVTX  0001000 // sticky bit

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)


void ext2fs_readsb(int dev, struct ext2_super_block *sb);
int ext2fs_dirlink(struct inode *, char *, u32);
struct inode *ext2fs_dirlookup(struct inode *, char *, u32 *);
struct inode *ext2fs_ialloc(u32, short);
void ext2fs_iinit(int dev);
void ext2fs_ilock(struct inode *);
void ext2fs_iput(struct inode *);
void ext2fs_iunlock(struct inode *);
void ext2fs_iunlockput(struct inode *);
void ext2fs_iupdate(struct inode *);
int ext2fs_readi(struct inode *, char *, u32, u32);
void ext2fs_stati(struct inode *, struct stat *);
int ext2fs_writei(struct inode *, char *, u32, u32);
