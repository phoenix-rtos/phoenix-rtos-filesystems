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


/* Inode modes (EXT2 file types) */
enum {
	MODE_FIFO      = 0x1000,
	MODE_CHARDEV   = 0x2000,
	MODE_DIR       = 0x4000,
	MODE_BLOCKDEV  = 0x6000,
	MODE_FILE      = 0x8000,
	MODE_SOFTLINK  = 0xA000,
	MODE_SOCKET    = 0xC000,
	MODE_TYPE_MASK = 0xF000
};


/* Inode flags */
enum {
	FLAG_SECRM        = 0x00000001, /* Secure deletion */
	FLAG_UNRM         = 0x00000002, /* Record for undelete */
	FLAG_COMPR        = 0x00000004, /* Compressed file */
	FLAG_SYNC         = 0x00000008, /* Synchronous updates */
	FLAG_IMMUTABLE    = 0x00000010, /* Immutable file */
	FLAG_APPEND       = 0x00000020, /* Append only */
	FLAG_NODUMP       = 0x00000040, /* Do not dump/delete file */
	FLAG_NOATIME      = 0x00000080, /* Do not update atime */

	/* Compression flags */
	FLAG_DIRTY        = 0x00000100, /* Dirty (file is in use?) */
	FLAG_COMPRBLK     = 0x00000200, /* Compressed blocks */
	FLAG_NOCOMPR      = 0x00000400, /* Access raw compressed data */
	FLAG_ECOMPR       = 0x00000800, /* Compression error */

	FLAG_INDEX        = 0x00001000, /* Hash indexed directory */
	FLAG_IMAGIC       = 0x00002000, /* AFS directory */
	FLAG_JOURNAL_DATA = 0x00004000, /* File data should be journaled */
	FLAG_NOTAIL       = 0x00008000, /* File tail should not be merged */
	FLAG_DIRSYNC      = 0x00010000, /* Dirsync behaviour (directories only) */
	FLAG_TOPDIR       = 0x00020000, /* Top of directory hierarchies */
	FLAG_HUGE_FILE    = 0x00040000, /* Set to each huge file */
	FLAG_EXTENTS      = 0x00080000, /* Inode uses extents */
	FLAG_EA_INODE     = 0x00200000, /* Inode used to store large extended attributes */
	FLAG_EOFBLOCKS    = 0x00400000, /* Blocks allocated beyond EOF */
	FLAG_RESERVED     = 0x80000000  /* Reserved */
};


typedef struct {
	uint16_t mode;           /* File mode (type and access rights) */
	uint16_t uid;            /* Owner id (low 16 bits) */
	uint32_t size;           /* Inode size */
	uint32_t atime;          /* Last access time */
	uint32_t ctime;          /* Inode change time */
	uint32_t mtime;          /* Modification time */
	uint32_t dtime;          /* Deletion time */
	uint16_t gid;            /* Group id (low 16 bits) */
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
	uint32_t file_acl;       /* File ACL */
	uint32_t size_hi;        /* High part of size */
	uint32_t faddr;          /* File fragment address (obsolete)*/
	union {
		struct {
			uint8_t frag;
			uint8_t fsize;
			uint16_t pad;
			uint16_t uid_hi;
			uint16_t gid_hi;
			uint32_t reserved;
		} linux2;
		struct {
			uint8_t frag;
			uint8_t fsize;
			uint16_t mode_hi;
			uint16_t uid_hi;
			uint16_t gid_hi;
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


extern uint32_t ext2_create_inode(ext2_t *fs, uint32_t pino, uint16_t mode);


extern int ext2_destroy_inode(ext2_t *fs, uint32_t ino, uint16_t mode);


extern ext2_inode_t *ext2_init_inode(ext2_t *fs, uint32_t ino);


extern int ext2_sync_inode(ext2_t *fs, uint32_t ino, ext2_inode_t *inode);


#endif
