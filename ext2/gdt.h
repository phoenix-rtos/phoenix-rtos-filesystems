/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Group Descriptor Table
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _GDT_H_
#define _GDT_H_


#include <stdint.h>

#include "ext2.h"


typedef struct {
	uint32_t block_bmp;   /* Blocks bitmap block */
	uint32_t inode_bmp;   /* Inodes bitmap block */
	uint32_t inode_tbl;   /* Inodes table block */
	uint16_t free_blocks; /* Number of free blocks in the group */
	uint16_t free_inodes; /* Number of free inodes in the group */
	uint16_t dirs;        /* Number of directories in the group */
	uint16_t pad;         /* Padding */
	uint32_t reserved[3]; /* Reserved */
} __attribute__ ((packed)) ext2_gd_t;


extern int ext2_init_gdt(ext2_t *fs);


extern int ext2_sync_gd(ext2_t *fs, uint32_t group);


extern int ext2_sync_gdt(ext2_t *fs);


#endif
