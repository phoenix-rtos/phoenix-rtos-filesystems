/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Filesystem operations
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _EXT2_H_
#define _EXT2_H_

#include <limits.h>
#include <stdint.h>

#include <sys/types.h>


/* Misc definitions */
#define ROOT_INO    2   /* Root inode number */
#define MAX_OBJECTS 512 /* Max number of filesystem objects in use */


/* Filesystem common data types forward declaration */
typedef struct _ext2_sb_t   ext2_sb_t;   /* SuperBlock */
typedef struct _ext2_gd_t   ext2_gd_t;   /* Group Descriptor*/
typedef struct _ext2_obj_t  ext2_obj_t;  /* Filesystem object */
typedef struct _ext2_objs_t ext2_objs_t; /* Filesystem objects */


/* Device access callbacks */
typedef ssize_t (*dev_read)(id_t, offs_t, char *, size_t);
typedef ssize_t (*dev_write)(id_t, offs_t, const char *, size_t);


typedef struct {
	/* Device info */
	uint32_t sectorsz; /* Device sector size */
	dev_read read;     /* Device read callback */
	dev_write write;   /* Device write callback */

	/* Filesystem info */
	oid_t oid;         /* Filesystem port and device ID */
	ext2_sb_t *sb;     /* SuperBlock */
	ext2_gd_t *gdt;    /* Group Descriptors Table */
	uint32_t blocksz;  /* Block size */
	uint32_t groups;   /* Number of groups */

	/* Filesystem objects */
	ext2_obj_t *root;  /* Root object */
	ext2_objs_t *objs; /* Filesystem objects */
} ext2_t;


/* Include filesystem common data types definitions */
#include "gdt.h"
#include "obj.h"
#include "sb.h"


/* Creates a file */
extern int ext2_create(ext2_t *fs, id_t id, const char *name, uint8_t len, oid_t *dev, uint16_t mode, id_t *res);


/* Destroys a file */
extern int ext2_destroy(ext2_t *fs, id_t id);


/* Lookups a file */
extern int ext2_lookup(ext2_t *fs, id_t id, const char *name, uint8_t len, id_t *res, oid_t *dev);


/* Opens a file */
extern int ext2_open(ext2_t *fs, id_t id);


/* Closes a file */
extern int ext2_close(ext2_t *fs, id_t id);


/* Reads from a file */
extern ssize_t ext2_read(ext2_t *fs, id_t id, offs_t offs, char *buff, size_t len);


/* Writes to a file */
extern ssize_t ext2_write(ext2_t *fs, id_t id, offs_t offs, const char *buff, size_t len);


/* Truncates a file */
extern int ext2_truncate(ext2_t *fs, id_t id, size_t size);


/* Retrives file attributes */
extern int ext2_getattr(ext2_t *fs, id_t id, int type, int *attr);


/* Sets file attributes */
extern int ext2_setattr(ext2_t *fs, id_t id, int type, int attr);


/* Adds a link */
extern int ext2_link(ext2_t *fs, id_t id, const char *name, uint8_t len, id_t lid);


/* Removes a link */
extern int ext2_unlink(ext2_t *fs, id_t id, const char *name, uint8_t len);


/* Bitmap bit operations */
static inline uint32_t ext2_findzerobit(uint32_t *bmp, uint32_t size, uint32_t offs)
{
	uint32_t len = (size - 1) / (CHAR_BIT * sizeof(uint32_t)) + 1;
	uint32_t tmp, i;

	for (i = offs / (CHAR_BIT * sizeof(uint32_t)); i < len; i++) {
		if ((tmp = bmp[i] ^ ~0UL)) {
			offs = i * (CHAR_BIT * sizeof(uint32_t)) + __builtin_ffsl(tmp);
			return (offs > size) ? 0 : offs;
		}
	}

	return 0;
}


static inline uint8_t ext2_checkbit(uint32_t *bmp, uint32_t offs)
{
	uint32_t woffs = (offs - 1) % (CHAR_BIT * sizeof(uint32_t));
	uint32_t aoffs = (offs - 1) / (CHAR_BIT * sizeof(uint32_t));

	return !!(bmp[aoffs] & (1UL << woffs));
}


static inline void ext2_togglebit(uint32_t *bmp, uint32_t offs)
{
	uint32_t woffs = (offs - 1) % (CHAR_BIT * sizeof(uint32_t));
	uint32_t aoffs = (offs - 1) / (CHAR_BIT * sizeof(uint32_t));
	bmp[aoffs] ^= 1UL << woffs;
}


#endif
