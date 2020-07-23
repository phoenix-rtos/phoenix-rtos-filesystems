/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Inode
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _INODE_H_
#define _INODE_H_

#include <stdint.h>

#include "ext2.h"


/* Inode blocks */
#define DIRECT_BLOCKS 12                                   /* Number of direct blocks */
#define SINGLE_INDIRECT_BLOCK DIRECT_BLOCKS                /* Single indirect block */
#define DOUBLE_INDIRECT_BLOCK (SINGLE_INDIRECT_BLOCK + 1)  /* Double indirect block */
#define TRIPPLE_INDIRECT_BLOCK (DOUBLE_INDIRECT_BLOCK + 1) /* Tripple indirect block */
#define NBLOCKS (TRIPPLE_INDIRECT_BLOCK + 1)               /* Total number of blocks */
#define INDIRECT_BLOCKS (NBLOCKS - DIRECT_BLOCKS)          /* Number of indirect blocks */


/* Inode flags */
enum {
	IFLAG_SECRM        = 0x00000001, /* Secure deletion */
	IFLAG_UNRM         = 0x00000002, /* Record for undelete */
	IFLAG_COMPR        = 0x00000004, /* Compressed file */
	IFLAG_SYNC         = 0x00000008, /* Synchronous updates */
	IFLAG_IMMUTABLE    = 0x00000010, /* Immutable file */
	IFLAG_APPEND       = 0x00000020, /* Append only */
	IFLAG_NODUMP       = 0x00000040, /* Do not dump/delete file */
	IFLAG_NOATIME      = 0x00000080, /* Do not update atime */

	/* Compression flags */
	IFLAG_DIRTY        = 0x00000100, /* Dirty (file is in use?) */
	IFLAG_COMPRBLK     = 0x00000200, /* Compressed blocks */
	IFLAG_NOCOMPR      = 0x00000400, /* Access raw compressed data */
	IFLAG_ECOMPR       = 0x00000800, /* Compression error */

	IFLAG_INDEX        = 0x00001000, /* Hash indexed directory */
	IFLAG_IMAGIC       = 0x00002000, /* AFS directory */
	IFLAG_JOURNAL_DATA = 0x00004000, /* File data should be journaled */
	IFLAG_NOTAIL       = 0x00008000, /* File tail should not be merged */
	IFLAG_DIRSYNC      = 0x00010000, /* Dirsync behaviour (directories only) */
	IFLAG_TOPDIR       = 0x00020000, /* Top of directory hierarchies */
	IFLAG_HUGE_FILE    = 0x00040000, /* Set to each huge file */
	IFLAG_EXTENTS      = 0x00080000, /* Inode uses extents */
	IFLAG_EAINODE      = 0x00200000, /* Inode used to store large extended attributes */
	IFLAG_EOFBLOCKS    = 0x00400000, /* Blocks allocated beyond EOF */
	IFLAG_RESERVED     = 0x80000000  /* Reserved */
};


typedef struct {
	uint16_t mode;           /* File mode (type and access rights) */
	uint16_t uid;            /* Owner ID (low 16 bits) */
	uint32_t size;           /* Inode size */
	uint32_t atime;          /* Last access time */
	uint32_t ctime;          /* Creation time */
	uint32_t mtime;          /* Modification time */
	uint32_t dtime;          /* Deletion time */
	uint16_t gid;            /* Group ID (low 16 bits) */
	uint16_t links;          /* Number of links */
	uint32_t blocks;         /* Number of blocks */
	uint32_t flags;          /* File flags */
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
	} osd1;                  /* OS dependent */
	uint32_t block[NBLOCKS]; /* Pointers to blocks */
	uint32_t generation;     /* File version (used by NFS) */
	uint32_t fileACL;        /* File ACL */
	uint32_t sizeHi;         /* High part of size */
	uint32_t faddr;          /* File fragment address (obsolete)*/
	union {
		struct {
			uint8_t frag;
			uint8_t fsize;
			uint16_t pad;
			uint16_t uidHi;
			uint16_t gidHi;
			uint32_t reserved;
		} linux2;
		struct {
			uint8_t frag;
			uint8_t fsize;
			uint16_t modeHi;
			uint16_t uidHi;
			uint16_t gidHi;
			uint32_t author;
		} hurd2;
		struct {
			uint8_t frag;
			uint8_t fsize;
			uint16_t pad;
			uint32_t reserved[2];
		} masix2;
	} osd2;                  /* OS dependent */
} __attribute__ ((packed)) ext2_inode_t;


/* Synchronizes inode */
extern int ext2_inode_sync(ext2_t *fs, uint32_t ino, ext2_inode_t *inode);


/* Initializes inode */
extern ext2_inode_t *ext2_inode_init(ext2_t *fs, uint32_t ino);


/* Destroys inode */
extern int ext2_inode_destroy(ext2_t *fs, uint32_t ino, uint16_t mode);


/* Creates inode */
extern uint32_t ext2_inode_create(ext2_t *fs, uint32_t pino, uint16_t mode);


#endif
