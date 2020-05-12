/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Directory
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DE_H_
#define _DE_H_


#include <stdint.h>


/* Directory entry types */
enum {
	DE_UNKNOWN  = 0,
	DE_REG_FILE = 1,
	DE_DIR      = 2,
	DE_CHRDEV   = 3,
	DE_BLKDEV   = 4,
	DE_FIFO     = 5,
	DE_SOCK     = 6,
	DE_SYMLINK  = 7
};


typedef struct {
	uint32_t ino;     /* Inode for the directory entry */
	uint16_t entrysz; /* Directory entry size */
	uint8_t len;      /* Name length */
	uint8_t type;     /* Directory entry type */
	char *name;       /* Directroy entry name */
} ext2_de_t;


extern int dir_is_empty(ext2_obj_t *dir);


extern int dir_find(ext2_obj_t *dir, const char *name, uint32_t len, id_t *resId);


extern int dir_add(ext2_obj_t *dir, const char *name, size_t len, uint16_t type, id_t *id);


extern int dir_remove(ext2_obj_t *dir, const char *name, size_t len);


#endif
