/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
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

#include <stdint.h>

#include <sys/types.h>

#include "gdt.h"
#include "obj.h"
#include "sb.h"


#define ROOT_INO 2

#define DIRECT_BLOCKS 12                                   /* Number of direct blocks */
#define SINGLE_INDIRECT_BLOCK DIRECT_BLOCKS                /* Single indirect block */
#define DOUBLE_INDIRECT_BLOCK (SINGLE_INDIRECT_BLOCK + 1)  /* Double indirect block */
#define TRIPPLE_INDIRECT_BLOCK (DOUBLE_INDIRECT_BLOCK + 1) /* Tripple indirect block */
#define NBLOCKS (TRIPPLE_INDIRECT_BLOCK + 1)               /* Total number of blocks */
#define INDIRECT_BLOCKS (NBLOCKS - DIRECT_BLOCKS)          /* Number of indirect blocks */

typedef ssize_t (*read_dev)(id_t, offs_t, char *, size_t);
typedef ssize_t (*write_dev)(id_t, offs_t, const char *, size_t);


typedef struct {
	/* Device info */
	uint32_t port;     /* Device port */
	id_t dev;          /* Device ID */
	uint32_t sectorsz; /* Device sector size */
	read_dev read;     /* Read from device */
	write_dev write;   /* Write to device */

	/* Filesystem info */
	ext2_sb_t *sb;     /* SuperBlock */
	ext2_gd_t *gdt;    /* Group Descriptors Table */
	uint32_t blocksz;  /* Block size */
	uint32_t groups;   /* Number of groups */

	/* Filesystem objects */
	ext2_objs_t	*objs; /* Filesystem objects */
	ext2_obj_t *root;  /* Root object */
} ext2_t;


#define EXT2_NAME_LEN 255

#define EXT2_MAX_FILES 512
#define EXT2_CACHE_SIZE 127


extern uint32_t ext2_find_zero_bit(uint32_t *addr, uint32_t size, uint32_t offs);


extern uint8_t ext2_check_bit(uint32_t *addr, uint32_t offs);


extern uint32_t ext2_toggle_bit(uint32_t *addr, uint32_t offs);


static inline void split_path(const char *path, uint32_t *start, uint32_t *end, uint32_t len)
{
	const char *s = path + *start;
	while (*s++ == '/' && *start < len)
		(*start)++;

	s--;
	*end = *start;
	while (*s++ != '/' && *end < len)
		(*end)++;
}

#endif
