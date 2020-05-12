/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * SuperBlock
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _SB_H_
#define _SB_H_


#include <stdint.h>

#include "ext2.h"


/* Superblock offset */
#define SB_OFFSET 1024


/* Filesystem magic identifier */
enum {
	MAGIC_EXT2 = 0xEF53 /* EXT2 filesystem */
};


/* Filesystem state */
enum {
	STATE_VALID    = 1, /* The filesystem was unmounted cleanly */
	STATE_ERROR    = 2, /* Errors detected */
	STATE_RECOVER  = 3  /* Orphans being recovered */
};


/* On error action */
enum {
	ERROR_CONTINUE = 1, /* Continue as if nothing happend */
	ERROR_RO       = 2, /* Remount read-only */
	ERROR_PANIC    = 3  /* Cause a kernel panic */
};


/* Revision level */
enum {
	REV_ORIGINAL   = 0, /* Revision 0 */
	REV_DYNAMIC    = 1  /* Revision 1 (variable inode sizes, extended attributes, etc.) */
};


/* Creator OS */
enum {
	OS_LINUX       = 0, /* Linux */
	OS_HURD        = 1, /* GNU HURD */
	OS_MASIX       = 2, /* MASIX */
	OS_FREEBSD     = 3, /* FreeBSD */
	OS_LITES       = 4  /* Lites */
};


/* Compression algorithms */
enum {
	BMP_LZV1       = 0x01,
	BMP_LZRW3A     = 0x02,
	BMP_GZIP       = 0x04,
	BMP_BZIP2      = 0x08,
	BMP_LZO        = 0x10
};


/* Hash algorithms */
enum {
	HASH_SIGNED_LEGACY   = 0,
	HASH_SIGNED_MD4      = 1,
	HASH_SIGNED_TEA      = 2,
	HASH_UNSIGNED_LEGACY = 3,
	HASH_UNSIGNED_MD4    = 4,
	HASH_UNSIGNED_TEA    = 5
};


/* Miscellaneous flags */
enum {
	MISC_SIGNED_HASH     = 0x01, /* Signed directory hash in use */
	MISC_UNSIGNED_HASH   = 0x02, /* Unsigned directory hash in use */
	MISC_TEST            = 0x04  /* Development code testing */
};


/* Default mount options */
enum {
	DEFM_DEBUG             = 0x0001, /* Print debug info upon (re)mount */
	DEFM_BSDGROUPS         = 0x0002, /* New files take the GID of the containing directory */
	DEFM_XATTR_USER        = 0x0004, /* Support userspace-provided extended attributes */
	DEFM_ACL               = 0x0008, /* Support POSIX ACLs */
	DEFM_UID16             = 0x0010, /* Don't support 32-bit UIDs */
	DEFM_JMODE_DATA        = 0x0020, /* All data and metadata are commited to the journal */
	DEFM_JMODE_ORDERED     = 0x0040, /* All data are flushed to the disk before metadata are commited to the journal */
	DEFM_JMODE_WBACK       = 0x0060, /* Data ordering is not preserved - data may be written after the metadata */
	DEFM_NOBARRIER         = 0x0100, /* Disable write flushes */
	DEFM_BLOCK_VALIDITY    = 0x0200, /* Track which blocks are metadata and shouldn't be used as data blocks */
	DEFM_DISCARD           = 0x0400, /* Enable DISCARD support */
	DEFM_NODELALLOC        = 0x0800  /* Disable delayed allocation */
};


/* Compatible features */
enum {
	COMPAT_DIR_PREALLOC    = 0x0001, /* Directory pre-allocation  */
	COMPAT_IMAGIC_INODES   = 0x0002, /* Imagic inodes */
	COMPAT_HAS_JOURNAL     = 0x0004, /* Journal support */
	COMPAT_EXT_ATTR        = 0x0008, /* Extended inode attributes */
	COMPAT_RESIZE_INODE    = 0x0010, /* Non-standard inode size */
	COMPAT_DIR_INDEX       = 0x0020, /* Directory indexing (HTree) */
	COMPAT_LAZY_BG         = 0x0040, /* Lazy block groups */
	COMPAT_EXCLUDE_INODE   = 0x0080, /* Exclude inode */
	COMPAT_EXCLUDE_BMP     = 0x0100, /* Exclude bitmap */
	COMPAT_SPARSE_SUPER2   = 0x0200  /* Backup superblocks */
};


/* Incompatible features */
enum {
	INCOMPAT_COMPRESSION   = 0x0001, /* Disk/File compression */
	INCOMPAT_FILETYPE      = 0x0002, /* Directory entries record the file type */
	INCOMPAT_RECOVER       = 0x0004, /* Filesystem recovery */
	INCOMPAT_JOURNAL_DEV   = 0x0008, /* Separate journal device */
	INCOMPAT_META_BG       = 0x0010, /* Meta block groups */
	INCOMPAT_EXTENTS       = 0x0020, /* Files use extents */
	INCOMPAT_64BIT         = 0x0080, /* Enable filesystem size of 2^64 blocks */
	INCOMPAT_MMP           = 0x0100, /* Multiple mount protection */
	INCOMPAT_FLEX_BG       = 0x0200, /* Flexible block groups */
	INCOMPAT_EA_INODE      = 0x0400, /* Inodes can be used to store large extended attributes */
	INCOMPAT_DIRDATA       = 0x1000, /* Data in directory entry */
	INCOMPAT_CSUM_SEED     = 0x2000, /* Metadata checksum is stored in the superblock */
	INCOMPAT_LARGEDIR      = 0x4000, /* Large directory >2GB or 3-lvl HTree */
	INCOMPAT_INLINE_DATA   = 0x8000, /* Data in inode */
	INCOMPAT_ENCRYPT       = 0x10000 /* Encrypted inodes */
};


/* Read-only compatible features */
enum {
	ROCOMPAT_SPARSE_SUPER  = 0x0001, /* Sparse superblocks */
	ROCOMPAT_LARGE_FILE    = 0x0002, /* Large file support, 64-bit file size */
	ROCOMPAT_BTREE_DIR     = 0x0004, /* BTree sorted directories */
	ROCOMPAT_HUGE_FILE     = 0x0008, /* Use logical blocks as file size unit, not 512-byte sectors */
	ROCOMPAT_GDT_CSUM      = 0x0010, /* Group descriptors have checksums */
	ROCOMPAT_DIR_NLINK     = 0x0020, /* The 32k subdirectory limit does not apply */
	ROCOMPAT_EXTRA_ISIZE   = 0x0040, /* Large inodes support */
	ROCOMPAT_HAS_SNAPSHOT  = 0x0080, /* Snapshot support */
	ROCOMPAT_QUOTA         = 0x0100, /* Quota support */
	ROCOMPAT_BIGALLOC      = 0x0200, /* Use fragments as file extents unit, not blocks */
	ROCOMPAT_METADATA_CSUM = 0x0400, /* Metadata have checksums */
	ROCOMPAT_REPLICA       = 0x0800, /* Replicas support */
	ROCOMPAT_READONLY      = 0x1000, /* Read-only filesystem image */
	ROCOMPAT_PROJECT       = 0x2000, /* Filesystem tracks project quotas */
	ROCOMPAT_VERITY        = 0x8000, /* Verity inodes support */
};


typedef struct {
	/* REV_ORIGINAL */
	uint32_t inodes;              /* Number of inodes */
	uint32_t blocks;              /* Number of blocks */
	uint32_t res_blocks;          /* Number of reserved blocks */
	uint32_t free_blocks;         /* Number of free blocks */
	uint32_t free_inodes;         /* Number of free inodes */
	uint32_t fst_block;           /* First data block */
	uint32_t log_blocksz;         /* Block size (shift left 1024) */
	uint32_t log_fragsz;          /* Fragment size (shift left 1024) */
	uint32_t group_blocks;        /* Number of blocks in group */
	uint32_t group_frags;         /* Number of fragments in group */
	uint32_t group_inodes;        /* Number of inodes in group */
	uint32_t mount_time;          /* Last mount time */
	uint32_t write_time;          /* Last write time */
	uint16_t mounts;              /* Number of mounts since last full check */
	uint16_t max_mounts;          /* Max number of mounts before full check */
	uint16_t magic;               /* Filesystem magic identifier */
	uint16_t state;               /* Filesystem state (1: STATE_VALID, 2: STATE_ERROR) */
	uint16_t on_error;            /* On error action (1: ERROR_CONTINUE, 2: ERROR_RO, 3: ERROR_PANIC) */
	uint16_t rev_minor;           /* Minor revision level */
	uint32_t check_time;          /* Last check time */
	uint32_t check_interval;      /* Interval between checks */
	uint32_t creator_os;          /* Identifier of the OS that created the filesystem */
	uint32_t rev_major;           /* Major revision level (0: REV_GOOD_OLD, 1: REV_DYNAMIC) */
	uint16_t res_uid;             /* Default user id for reserved blocks */
	uint16_t res_gid;             /* Default group id for reserved blocks */

	/* REV_DYNAMIC */
	uint32_t fst_inode;           /* First standard inode */
	uint16_t inodesz;             /* Inode size */
	uint16_t block_group;         /* This superblock block group number */
	uint32_t feature_compat;      /* Compatible features mask */
	uint32_t feature_incompat;    /* Incompatible features mask */
	uint32_t feature_rocompat;    /* Read-only compatible features mask */
	uint8_t uuid[16];             /* Volume ID */
	char name[16];                /* Volume name */
	char path[64];                /* Last mount path */
	uint32_t bmp_algo;            /* Compression algorithm */

	/* Performance hints */
	uint8_t prealloc_blocks;      /* Number of blocks to preallocate for files */
	uint8_t prealloc_dir_blocks;  /* Number of blocks to preallocate for directories */
	uint16_t res_gdt_blocks;      /* Number of reserved GDT entries for future filesystem growth */

	/* Journalling support */
	uint8_t journal_uuid[16];     /* Journal superblock ID */
	uint32_t journal_inode;       /* Journal file inode */
	uint32_t journal_dev;         /* Device number of journal file */
	uint32_t last_orphan;         /* Head of list of inodes to delete */
	uint32_t hash_seed[4];        /* HTree hash seed */
	uint8_t hash_algo;            /* Hash algorith to use for directory hashes */
	uint8_t journal_backup;       /* 0 or 1: journal_blocks contain backup copy of the journal inodes */
	uint16_t descsz;              /* Size of group descriptor */
	uint32_t def_mount_opts;      /* Default mount options */
	uint32_t fst_meta_bg;         /* First metablock block group */
	uint32_t mkfs_time;           /* Filesystem creation time */
	uint32_t journal_blocks[17];  /* Backup of the journal inode */

	/* 64-bit support */
	uint32_t blocks_hi;           /* High part of number of blocks */
	uint32_t res_blocks_hi;       /* High part of number of reserved blocks */
	uint32_t free_blocks_hi;      /* High part of number of free blocks */
	uint16_t min_extra_inodesz;   /* Min size all inodes should have */
	uint16_t want_extra_inodesz;  /* New inodes size */
	uint32_t flags;               /* Miscellaneous flags */
	uint16_t raid_stride;         /* RAID stride */
	uint16_t mmp_interval;        /* Seconds to wait in MMP (Multi-Mount Protection) check */
	uint64_t mmp_block;           /* Block for MMP */
	uint32_t raid_stride_width;   /* Blocks on all data disks (N * stride) */
	uint8_t log_flex_groups;      /* FLEX_BG group size */
	uint8_t checksum_type;        /* Metadata checksum algorithm type. 1: crc32c */
	uint16_t reserved_pad;        /* Padding */
	uint64_t kbytes_written;      /* Number of KiB written (lifetime) */
	uint32_t snapshot_inode;      /* Inode of the active snapshot */
	uint32_t snapshot_id;         /* Sequential ID of the active snapshot */
	uint64_t snapshot_res_blocks; /* Number of blocks reserved for the active snapshot's future use */
	uint32_t snapshot_list;       /* Inode of the head of the on-disk snapshots list */
	uint32_t errors;              /* Number of filesystem errors */
	uint32_t fst_error_time;      /* First error time */
	uint32_t fst_error_inode;     /* Inode involved in the first error */
	uint64_t fst_error_block;     /* Block involved in the first error */
	uint8_t fst_error_func[32];   /* Function where the first error happened */
	uint32_t fst_error_line;      /* Line where the first error happened */
	uint32_t last_error_time;     /* Last error time */
	uint32_t last_error_inode;    /* Inode involved in the last error */
	uint32_t last_error_line;     /* Line where the last error happened */
	uint64_t last_error_block;    /* Block involved in the last error */
	uint8_t last_error_func[32];  /* Function where the last error happened */
	uint8_t mount_opts[64];       /* ASCIIZ string of mount options */
	uint32_t usr_quota_inode;     /* Inode used for tracking user quota */
	uint32_t grp_quota_inode;     /* Inode used for tracking group quota */
	uint32_t overhead_blocks;     /* Overhead blocks in filesystem */
	uint32_t backup_bgs[2];       /* Groups with SPARSE_SUPER2 superblocks */
	uint8_t encrypt_algos[4];     /* Encryption algorithms in use */
	uint8_t encrypt_pw_salt[16];  /* Salt used for string2key algorithm */
	uint32_t lpf_inode;           /* Lost+found inode */
	uint32_t pad[100];            /* Padding */
	uint32_t checksum;            /* This superblock checksum (crc32c) */
} __attribute__ ((packed)) ext2_sb_t;


extern int ext2_init_sb(ext2_t *fs);


extern int ext2_sync_sb(ext2_t *fs);


#endif
