/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Directory operations
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DIR_H_
#define _DIR_H_

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/types.h>

#include "ext2.h"


/* Directory entry types */
enum {
	DIRENT_UNKNOWN  = 0,
	DIRENT_FILE     = 1,
	DIRENT_DIR      = 2,
	DIRENT_CHRDEV   = 3,
	DIRENT_BLKDEV   = 4,
	DIRENT_FIFO     = 5,
	DIRENT_SOCK     = 6,
	DIRENT_SYMLINK  = 7
};


typedef struct {
	uint32_t ino;  /* Entry inode number */
	uint16_t size; /* Entry size */
	uint8_t len;   /* Entry name length */
	uint8_t type;  /* Entry type */
	char name[];   /* Entry name */
} ext2_dirent_t;


/* Checks if directory is empty (requires object to be locked) */
extern int _ext2_dir_empty(ext2_t *fs, ext2_obj_t *dir);


/* Searches directory for a given file name (requires object to be locked) */
extern int _ext2_dir_search(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len, id_t *res);


/* Reads directory entry (requires object to be locked) */
extern int _ext2_dir_read(ext2_t *fs, ext2_obj_t *dir, offs_t offs, struct dirent *res, size_t len);


/* Adds directory entry (requires object to be locked) */
extern int _ext2_dir_add(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len, uint16_t mode, uint32_t ino);


/* Removes directory entry (requires object to be locked) */
extern int _ext2_dir_remove(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len);


#endif
