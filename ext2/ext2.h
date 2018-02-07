/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * ext2.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _EXT2_H_
#define _EXT2_H_ /* ext2.h */

#include <sys/rb.h>

/* ext2 magic number */
#define EXT2_MAGIC 0xEF53

/* superblock structure */
typedef struct _superblock_t {
    u32     inodes_count;       /* inodes count */
    u32     blocks_count;       /* blocks count */
    u32     r_blocks_count;     /* reserved blocks count */
    u32     free_blocks_count;  /* free blocks count */
    u32     free_inodes_count;  /* free inodes count */
    u32     first_data_block;   /* first data block */
    u32     log_block_size;     /* shift left 1024 */
    u32     log_frag_size;
    u32     blocks_in_group;    /* number of blocks in group */
    u32     frags_in_group;     /* number of fragments in group */
    u32     inodes_in_group;    /* number of inodes in group */
    u32     mount_time;         /* POSIX */
    u32     write_time;         /* POSIX */
    u16     mount_count;
    u16     max_mount_count;
    u16     magic;
    u16     state;
    u16     errors;
    u16     minor_rev_level;
    u32     lastcheck;          /* POSIX */
    u32     checkinterval;      /* POSIX */
    u32     creator_os;
    u32     rev_level;
    u16     def_resuid;
    u16     def_resgid;
    /* DYNAMIC_REV */
    u32     first_inode;        /* first non reserved inode */
    u16     inode_size;         /* inode size */
    u16     block_group_no;     /* block group No of this superblock */
    u32     feature_compat;
    u32     feature_incompat;
    u32     feature_ro_compat;
    u8      uuid[16];
    char    volume_name[16];
    char    last_mounted[64];
    u32     algorithm_usage_bitmap;
} __attribute__ ((packed)) superblock_t;

/* block group descriptor structure */
typedef struct _group_desc_t {
    u32 block_bitmap;           /* block number of block bitmap */
    u32 inode_bitmap;           /* block number of inode bitmap */
    u32 ext2_inode_table;            /* block number of inode table */
    u16 free_blocks_count;      /* free block in the group */
    u16 free_inodes_count;      /* free inodes in the group */
    u16 used_dirs_count;        /* directories in the group */
    u16 pad;                    /* alignment to word */
    u32 reserved[3];            /* nulls to pad out 24 bytes */
} group_desc_t;

enum {
    EXT2_NDIR_BLOCKS    = 12,                       /* number of direct blocks */
    EXT2_IND_BLOCK      = EXT2_NDIR_BLOCKS,         /* single indirect block */
    EXT2_DIND_BLOCK     = (EXT2_IND_BLOCK + 1),     /* double indirect block */
    EXT2_TIND_BLOCK     = (EXT2_DIND_BLOCK + 1),    /* trible indirect block */
    EXT2_N_BLOCKS       = (EXT2_TIND_BLOCK + 1)     /* blocks in total */
};

/* inode modes */
enum {
    /* file format */
    EXT2_S_IFSOCK	= 0xC000,	/* socket */
    EXT2_S_IFLNK    = 0xA000,	/* symbolic link */
    EXT2_S_IFREG	= 0x8000,	/* regular file */
    EXT2_S_IFBLK	= 0x6000,	/* block device */
    EXT2_S_IFDIR	= 0x4000,	/* directory */
    EXT2_S_IFCHR	= 0x2000,	/* character device */
    EXT2_S_IFIFO	= 0x1000,	/* fifo */
    /* process execution user/group override */
    EXT2_S_ISUID	= 0x0800,	/* Set process User ID */
    EXT2_S_ISGID	= 0x0400,	/* Set process Group ID */
    EXT2_S_ISVTX	= 0x0200,	/* sticky bit */
    /* access rights */
    EXT2_S_IRUSR	= 0x0100,	/* user read */
    EXT2_S_IWUSR	= 0x0080,	/* user write */
    EXT2_S_IXUSR	= 0x0040,	/* user execute */
    EXT2_S_IRGRP	= 0x0020,	/* group read */
    EXT2_S_IWGRP	= 0x0010,	/* group write */
    EXT2_S_IXGRP	= 0x0008,	/* group execute */
    EXT2_S_IROTH	= 0x0004,	/* others read */
    EXT2_S_IWOTH	= 0x0002,	/* others write */
    EXT2_S_IXOTH	= 0x0001    /* others execute */
};

/* inode flags */
enum {
    EXT2_SECRM_FL           = 0x00000001, /* secure deletion */
    EXT2_UNRM_FL            = 0x00000002, /* record for undelete */
    EXT2_COMPR_FL           = 0x00000004, /* compressed file */
    EXT2_SYNC_FL            = 0x00000008, /* synchronous updates */
    EXT2_IMMUTABLE_FL       = 0x00000010, /* immutable file */
    EXT2_APPEND_FL          = 0x00000020, /* append only */
    EXT2_NODUMP_FL          = 0x00000040, /* do not dump/delete file */
    EXT2_NOATIME_FL         = 0x00000080, /* do not update .i_atime */
    EXT2_DIRTY_FL           = 0x00000100, /* dirty (file is in use?) */
    EXT2_COMPRBLK_FL        = 0x00000200, /* compressed blocks */
    EXT2_NOCOMPR_FL         = 0x00000400, /* access raw compressed data */
    EXT2_ECOMPR_FL          = 0x00000800, /* compression error */
    EXT2_BTREE_FL           = 0x00001000, /* b-tree format directory */
    EXT2_INDEX_FL           = 0x00001000, /* Hash indexed directory */
    EXT2_IMAGIC_FL          = 0x00002000, /* ? */
    EXT3_JOURNAL_DATA_FL    = 0x00004000, /* journal file data */
    EXT2_RESERVED_FL        = 0x80000000
};

/* inode structure */
typedef struct _ext2_inode_t {
    u16 mode;                   /* file mode (type and access rights) */
    u16 uid;                    /* owner id (low 16 bits) */
    u32 size;                   /* file length in bytes */
    u32 atime;                  /* last access time */
    u32 ctime;                  /* creation time */
    u32 mtime;                  /* modification time */
    u32 dtime;                  /* deletion time */
    u16 gid;                    /* user group id (low 16 bits) */
    u16 links_count;            /* hard links count */
    u32 blocks;                 /* number of data block of a file */
    u32 flags;                  /* file flags */
    union {
        struct {
            u32 reserved;
        } linux1;
        struct {
            u32 translator;
        } hurd1;
        struct {
            u32 reserved;
        } masix1;
    } osd1;                     /* OS dependent 1 */
    u32 block[EXT2_N_BLOCKS];   /* pointers to blocks */
    u32 generation;             /* file version (used by NFS) */
    u32 file_acl;               /* extended attributes */
    u32 dir_acl;                /* multiple purposes (rev0 always 0) */
    u32 faddr;                  /* location of file fragment */
    union {
        struct {
            u8  frag;           /* fragment number */
            u8  fsize;          /* fragment size */
            u16 pad;
            u16 uid_high;
            u16 gid_high;
            u32 reserved;
        } linux2;
        struct {
            u8  frag;
            u8  fsize;
            u16 mode_high;
            u16 uid_high;
            u16 gid_high;
            u32 author;
        } hurd2;
        struct {
            u8  frag;
            u8  fsize;
            u16 pad;
            u32 reserved[2];
        } masix2;
    } osd2;                     /* OS dependent 2 */
} ext2_inode_t;

typedef struct _ext2_object_t {
	oid_t oid;

	u32 refs;
	u8  dirty;
	u8  locked;
	handle_t lock;

	rbnode_t node;
	handle_t mutex;

	ext2_inode_t *inode;
} ext2_object_t;

#define EXT2_NAME_LEN 255

/* ext2 file types */
enum {
    EXT2_FT_UNKNOWN,
    EXT2_FT_REG_FILE,
    EXT2_FT_DIR,
    EXT2_FT_CHRDEV,
    EXT2_FT_BLKDEV,
    EXT2_FT_FIFO,
    EXT2_FT_SOCK,
    EXT2_FT_SYMLINK,
    EXT2_FT_MAX
};

/* directory entry structure */
typedef struct _ext2_dir_entry_t {
    u32     inode;      /* inode number */
    u16     rec_len;    /* directory entry length */
    u8      name_len;   /* name length */
    u8      file_type;
    char    name[];     /* file name (max length EXT2_NAME_LEN) */
} ext2_dir_entry_t;

typedef struct _ext2_info_t {
    u32             block_size;
    u32             blocks_count;
    u32             blocks_in_group;
    u32             inode_size;
    u32             inodes_count;
    u32             inodes_in_group;
    u32             gdt_size;
    group_desc_t    *gdt;
    u32             first_block;
    u32             sb_sect;
    ext2_inode_t    *root_node;
    superblock_t    *sb;
    struct mbr_t    *mbr;
	u32			    port;
} ext2_info_t;

#define SECTOR_SIZE 512
#define BLOCKBASE 1024
#define ROOTNODE_NO 2

ext2_info_t     *ext2;

static inline void split_path(const char *path, u32 *start, u32 *end, u32 len)
{
    const char *s = path + *start;
    while(*s++ == '/' && *start < len)
        (*start)++;

    s--;
    *end = *start;
    while (*s++ != '/' && *end < len)
        (*end)++;
}

static inline u32 find_zero_bit(u32 *addr, u32 size)
{
    u32 tmp = 0;
    u32 len = size / (sizeof(u32) * 8);
    u32 i;

    for(i = 0; i < len; i++) {
        tmp = addr[i] ^ ~0UL;
        if (tmp)
            break;
    }
    if(i == len)
        return 0;

    return (i * (sizeof(u32) * 8)) + (u32)__builtin_ffsl(tmp);
}

static inline u8 check_bit(u32 *addr, u32 off)
{
    u32 woff = (off - 1) % (sizeof(u32) * 8);
    u32 aoff = (off - 1) / (sizeof(u32) * 8);

    return (addr[aoff] & 1UL << woff) != 0;
}

static inline u32 toggle_bit(u32 *addr, u32 off)
{
    u32 woff = (off - 1) % (sizeof(u32) * 8);
    u32 aoff = (off - 1) / (sizeof(u32) * 8);
    u32 old = addr[aoff];

    addr[aoff] ^= 1UL << woff;

    return (old & 1UL << woff) != 0;
}

#endif /* ext2.h */
