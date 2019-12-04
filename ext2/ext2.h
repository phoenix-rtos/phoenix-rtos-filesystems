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
#include <stdint.h>


/* superblock structure */
typedef struct {
	uint32_t   	inodes_count;       /* inodes count */
	uint32_t   	blocks_count;       /* blocks count */
	uint32_t   	r_blocks_count;     /* reserved blocks count */
	uint32_t   	free_blocks_count;  /* free blocks count */
	uint32_t   	free_inodes_count;  /* free inodes count */
	uint32_t   	first_data_block;   /* first data block */
	uint32_t   	log_block_size;     /* shift left 1024 */
	uint32_t   	log_frag_size;
	uint32_t   	blocks_in_group;    /* number of blocks in group */
	uint32_t  	frags_in_group;     /* number of fragments in group */
	uint32_t  	inodes_in_group;    /* number of inodes in group */
	uint32_t  	mount_time;         /* POSIX */
	uint32_t  	write_time;         /* POSIX */
	uint16_t  	mount_count;
	uint16_t  	max_mount_count;
	uint16_t  	magic;
	uint16_t  	state;
	uint16_t 	errors;
	uint16_t  	minor_rev_level;
	uint32_t  	lastcheck;          /* POSIX */
	uint32_t  	checkinterval;      /* POSIX */
	uint32_t  	creator_os;
	uint32_t  	rev_level;
	uint16_t  	def_resuid;
	uint16_t  	def_resgid;
	/* DYNAMIC_REV */
	uint32_t 	first_inode;        /* first non reserved inode */
	uint16_t  	inode_size;         /* inode size */
	uint16_t  	block_group_no;     /* block group No of this superblock */
	uint32_t 	feature_compat;
	uint32_t 	feature_incompat;
	uint32_t	feature_ro_compat;
	uint8_t		uuid[16];
	char 		volume_name[16];
	char 		last_mounted[64];
	uint32_t	algorithm_usage_bitmap;
} __attribute__ ((packed)) ext2_superblock_t;


/* block group descriptor structure */
typedef struct _group_desc_t {
	uint32_t block_bitmap;           /* block number of block bitmap */
	uint32_t inode_bitmap;           /* block number of inode bitmap */
	uint32_t ext2_inode_table;       /* block number of inode table */
	uint16_t free_blocks_count;      /* free block in the group */
	uint16_t free_inodes_count;      /* free inodes in the group */
	uint16_t used_dirs_count;        /* directories in the group */
	uint16_t pad;                    /* alignment to word */
	uint32_t reserved[3];            /* nulls to pad out 24 bytes */
} ext2_group_desc_t;


enum {
	EXT2_NDIR_BLOCKS    = 12,                       /* number of direct blocks */
	EXT2_IND_BLOCK      = EXT2_NDIR_BLOCKS,         /* single indirect block */
	EXT2_DIND_BLOCK     = (EXT2_IND_BLOCK + 1),     /* double indirect block */
	EXT2_TIND_BLOCK     = (EXT2_DIND_BLOCK + 1),    /* trible indirect block */
	EXT2_N_BLOCKS       = (EXT2_TIND_BLOCK + 1)     /* blocks in total */
};


#define	EXT2_S_ISDIR(m)	(((m) & 0xF000) == 0x4000)	/* directory */
#define	EXT2_S_ISCHR(m)	(((m) & 0xF000) == 0x2000)	/* char special */
#define	EXT2_S_ISREG(m)	(((m) & 0xF000) == 0x8000)	/* regular file */


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
typedef struct {
	uint16_t mode;                   /* file mode (type and access rights) */
	uint16_t uid;                    /* owner id (low 16 bits) */
	uint32_t size;                   /* file length in bytes */
	uint32_t atime;                  /* last access time */
	uint32_t ctime;                  /* creation time */
	uint32_t mtime;                  /* modification time */
	uint32_t dtime;                  /* deletion time */
	uint16_t gid;                    /* user group id (low 16 bits) */
	uint16_t nlink;            /* hard links count */
	uint32_t blocks;                 /* number of data block of a file */
	uint32_t flags;                  /* file flags */
	union {
		struct {
			uint32_t reserved;
		} linux1;
		struct {
			uint32_t translator;
		} hurd1;
		struct {
			uint32_t reserved;
		} masix1;
	} osd1;                     /* OS dependent 1 */
	uint32_t block[EXT2_N_BLOCKS];   /* pointers to blocks */
	uint32_t generation;             /* file version (used by NFS) */
	uint32_t file_acl;               /* extended attributes */
	uint32_t dir_acl;                /* multiple purposes (rev0 always 0) */
	uint32_t faddr;                  /* location of file fragment */
	union {
		struct {
			uint8_t  frag;           /* fragment number */
			uint8_t  fsize;          /* fragment size */
			uint16_t pad;
			uint16_t uid_high;
			uint16_t gid_high;
			uint32_t reserved;
		} linux2;
		struct {
			uint8_t  frag;
			uint8_t  fsize;
			uint16_t mode_high;
			uint16_t uid_high;
			uint16_t gid_high;
			uint32_t author;
		} hurd2;
		struct {
			uint8_t  frag;
			uint8_t  fsize;
			uint16_t pad;
			uint32_t reserved[2];
		} masix2;
	} osd2;                     /* OS dependent 2 */
} ext2_inode_t;


typedef struct {
	uint32_t bno;
	uint32_t *data;
} ext2_block_t;


typedef struct _ext2_fs_info ext2_fs_info_t;


typedef struct {
	id_t id;

	uint32_t refs;
	uint8_t  flags;

	rbnode_t node;
	handle_t lock;

	ext2_inode_t *inode;
	ext2_fs_info_t *f;
	union {
		ext2_block_t ind[3];
		oid_t mnt;
	};
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

/* ext2 object flags */
enum {
	EXT2_FL_DIRTY = 1,
	EXT2_FL_MOUNTPOINT = 2,
	EXT2_FL_MOUNT = 4
};


/* directory entry structure */
typedef struct {
	uint32_t  	inode;      /* inode number */
	uint16_t   	rec_len;    /* directory entry length */
	uint8_t   	name_len;   /* name length */
	uint8_t 	file_type;
	char 		name[];     /* file name (max length EXT2_NAME_LEN) */
} ext2_dir_entry_t;


#define EXT2_MAX_FILES 512
#define EXT2_CACHE_SIZE 127


typedef struct {
		handle_t 		ulock;
		handle_t 		clock;
		rbtree_t 		used;
		uint32_t		used_cnt;
		ext2_object_t 	*cache[EXT2_CACHE_SIZE];
} ext2_fs_objects_t;

struct _ext2_fs_info {
	id_t				devId;
	oid_t				parent;
	ext2_group_desc_t 	*gdt;
	ext2_object_t    	*root;
	ext2_superblock_t 	*sb;
	ext2_fs_objects_t	*objects;
	uint32_t  			block_size;
	uint32_t     		blocks_count;
	uint32_t    		blocks_in_group;
	uint32_t     		inode_size;
	uint32_t     		inodes_count;
	uint32_t     		inodes_in_group;
	uint32_t        	gdt_size;
};


static inline uint32_t find_zero_bit(uint32_t *addr, uint32_t size, uint32_t off)
{
	uint32_t tmp = 0;
	uint32_t len = size / (sizeof(uint32_t) * 8);
	uint32_t i;

	for (i = off / (sizeof(uint32_t) * 8); i < len; i++) {
		tmp = addr[i] ^ ~0UL;
		if (tmp)
			break;
	}
	if (i == len)
		return 0;

	return (i * (sizeof(uint32_t) * 8)) + (uint32_t)__builtin_ffsl(tmp);
}


static inline uint8_t check_bit(uint32_t *addr, uint32_t off)
{
	uint32_t woff = (off - 1) % (sizeof(uint32_t) * 8);
	uint32_t aoff = (off - 1) / (sizeof(uint32_t) * 8);

	return (addr[aoff] & 1UL << woff) != 0;
}


static inline uint32_t toggle_bit(uint32_t *addr, uint32_t off)
{
	uint32_t woff = (off - 1) % (sizeof(uint32_t) * 8);
	uint32_t aoff = (off - 1) / (sizeof(uint32_t) * 8);
	uint32_t old = addr[aoff];

	addr[aoff] ^= 1UL << woff;

	return (old & 1UL << woff) != 0;
}

#endif /* ext2.h */
