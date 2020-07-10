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


/* Filesystem magic identifiers */
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
	MISC_SIGNED_HASH     = 0x01,     /* Signed directory hash in use */
	MISC_UNSIGNED_HASH   = 0x02,     /* Unsigned directory hash in use */
	MISC_TEST            = 0x04      /* Development code testing */
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
	ROCOMPAT_VERITY        = 0x8000  /* Verity inodes support */
};


struct _ext2_sb_t {
	/* REV_ORIGINAL */
	uint32_t inodes;            /* Number of inodes */
	uint32_t blocks;            /* Number of blocks */
	uint32_t resBlocks;         /* Number of reserved blocks */
	uint32_t freeBlocks;        /* Number of free blocks */
	uint32_t freeInodes;        /* Number of free inodes */
	uint32_t fstBlock;          /* First data block */
	uint32_t logBlocksz;        /* Block size (shift left 1024) */
	uint32_t logFragsz;         /* Fragment size (shift left 1024) */
	uint32_t groupBlocks;       /* Number of blocks in group */
	uint32_t groupFrags;        /* Number of fragments in group */
	uint32_t groupInodes;       /* Number of inodes in group */
	uint32_t mountTime;         /* Last mount time */
	uint32_t writeTime;         /* Last write time */
	uint16_t mounts;            /* Number of mounts since last full check */
	uint16_t maxMounts;         /* Max number of mounts before full check */
	uint16_t magic;             /* Filesystem magic identifier */
	uint16_t state;             /* Filesystem state (1: STATE_VALID, 2: STATE_ERROR) */
	uint16_t onError;           /* On error action (1: ERROR_CONTINUE, 2: ERROR_RO, 3: ERROR_PANIC) */
	uint16_t revMinor;          /* Minor revision level */
	uint32_t checkTime;         /* Last check time */
	uint32_t checkInterval;     /* Interval between checks */
	uint32_t creatorOS;         /* Identifier of the OS that created the filesystem */
	uint32_t revMajor;          /* Major revision level (0: REV_GOOD_OLD, 1: REV_DYNAMIC) */
	uint16_t resUID;            /* Default User ID for reserved blocks */
	uint16_t resGID;            /* Default Group ID for reserved blocks */

	/* REV_DYNAMIC */
	uint32_t fstInode;          /* First standard inode */
	uint16_t inodesz;           /* Inode size */
	uint16_t blockGroup;        /* This superblock block group number */
	uint32_t featureCompat;     /* Compatible features mask */
	uint32_t featureIncompat;   /* Incompatible features mask */
	uint32_t featureRocompat;   /* Read-only compatible features mask */
	uint8_t uuid[16];           /* Volume ID */
	char name[16];              /* Volume name */
	char path[64];              /* Last mount path */
	uint32_t bmpAlgo;           /* Compression algorithm */

	/* Performance hints */
	uint8_t preallocBlocks;     /* Number of blocks to preallocate for files */
	uint8_t preallocDirBlocks;  /* Number of blocks to preallocate for directories */
	uint16_t resGdtBlocks;      /* Number of reserved GDT entries for future filesystem growth */

	/* Journalling support */
	uint8_t journalUuid[16];    /* Journal superblock ID */
	uint32_t journalInode;      /* Journal file inode */
	uint32_t journalDev;        /* Device number of journal file */
	uint32_t lastOrphan;        /* Head of list of inodes to delete */
	uint32_t hashSeed[4];       /* HTree hash seed */
	uint8_t hashAlgo;           /* Hash algorith to use for directory hashes */
	uint8_t journalBackup;      /* 0 or 1: journal blocks contain backup copy of the journal inodes */
	uint16_t descsz;            /* Size of group descriptor */
	uint32_t defMountOpts;      /* Default mount options */
	uint32_t fstMetaBg;         /* First metablock block group */
	uint32_t mkfsTime;          /* Filesystem creation time */
	uint32_t journalBlocks[17]; /* Backup of the journal inode */

	/* 64-bit support */
	uint32_t blocksHi;          /* High part of number of blocks */
	uint32_t resBlocksHi;       /* High part of number of reserved blocks */
	uint32_t freeBlocksHi;      /* High part of number of free blocks */
	uint16_t minExtraInodesz;   /* Min size all inodes should have */
	uint16_t wantExtraInodesz;  /* New inodes size */
	uint32_t flags;             /* Miscellaneous flags */
	uint16_t raidStride;        /* RAID stride */
	uint16_t mmpInterval;       /* Seconds to wait in MMP (Multi-Mount Protection) check */
	uint64_t mmpBlock;          /* Block for MMP (Multi-Mount Protection) */
	uint32_t raidStrideWidth;   /* Blocks on all data disks (N * stride) */
	uint8_t logFlexGroups;      /* FLEX_BG group size */
	uint8_t checksumType;       /* Metadata checksum algorithm type, 1: crc32c */
	uint16_t reservedPad;       /* Padding */
	uint64_t kbytesWritten;     /* Number of KiB written (lifetime) */
	uint32_t snapshotInode;     /* Inode of the active snapshot */
	uint32_t snapshotId;        /* Sequential ID of the active snapshot */
	uint64_t snapshotResBlocks; /* Number of blocks reserved for the active snapshot's future use */
	uint32_t snapshotList;      /* Inode of the head of the on-disk snapshots list */
	uint32_t errors;            /* Number of filesystem errors */
	uint32_t fstErrorTime;      /* First error time */
	uint32_t fstErrorInode;     /* Inode involved in the first error */
	uint64_t fstErrorBlock;     /* Block involved in the first error */
	uint8_t fstErrorFunc[32];   /* Function where the first error happened */
	uint32_t fstErrorLine;      /* Line where the first error happened */
	uint32_t lastErrorTime;     /* Last error time */
	uint32_t lastErrorInode;    /* Inode involved in the last error */
	uint32_t lastErrorLine;     /* Line where the last error happened */
	uint64_t lastErrorBlock;    /* Block involved in the last error */
	uint8_t lastErrorFunc[32];  /* Function where the last error happened */
	uint8_t mountOpts[64];      /* ASCIIZ string of mount options */
	uint32_t userQuotaInode;    /* Inode used for tracking user quota */
	uint32_t groupQuotaInode;   /* Inode used for tracking group quota */
	uint32_t overheadBlocks;    /* Overhead blocks in filesystem */
	uint32_t backupBgs[2];      /* Groups with SPARSE_SUPER2 superblocks */
	uint8_t encryptAlgos[4];    /* Encryption algorithms in use */
	uint8_t encryptPwSalt[16];  /* Salt used for string2key algorithm */
	uint32_t lpfInode;          /* Lost+found inode */
	uint32_t pad[100];          /* Padding */
	uint32_t checksum;          /* This superblock checksum (crc32c) */
} __attribute__ ((packed));


/* Synchronizes superblock */
extern int ext2_sb_sync(ext2_t *fs);


/* Destroys superblock */
extern void ext2_sb_destroy(ext2_t *fs);


/* Initializes superblock */
extern int ext2_sb_init(ext2_t *fs);


#endif
