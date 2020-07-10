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


struct _ext2_gd_t {
	uint32_t blockBmp;    /* Blocks bitmap block */
	uint32_t inodeBmp;    /* Inodes bitmap block */
	uint32_t inodeTbl;    /* Inodes table block */
	uint16_t freeBlocks;  /* Number of free blocks */
	uint16_t freeInodes;  /* Number of free inodes */
	uint16_t dirs;        /* Number of directories */
	uint16_t pad;         /* Padding */
	uint32_t reserved[3]; /* Reserved */
} __attribute__ ((packed));


/* Synchronizes one group descriptor */
extern int ext2_gdt_syncone(ext2_t *fs, uint32_t group);


/* Synchronizes GDT */
extern int ext2_gdt_sync(ext2_t *fs);


/* Destroys GDT */
extern void ext2_gdt_destroy(ext2_t *fs);


/* Initializes GDT */
extern int ext2_gdt_init(ext2_t *fs);


#endif
