/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * File operations
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FILE_H_
#define _FILE_H_

#include <stdint.h>

#include <sys/types.h>

#include "ext2.h"


/* Reads a file (requires object to be locked) */
extern ssize_t _ext2_file_read(ext2_t *fs, ext2_obj_t *obj, offs_t offs, char *buff, size_t len);


/* Writes a file (requires object to be locked) */
extern ssize_t _ext2_file_write(ext2_t *fs, ext2_obj_t *obj, offs_t offs, const char *buff, size_t len);


/* Truncates a file (requires object to be locked) */
extern int _ext2_file_truncate(ext2_t *fs, ext2_obj_t *obj, size_t size);


#endif
