/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * file.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FILE_H_
#define _FILE_H_ /* file.h */

#include <stdint.h>

extern int ext2_read_internal(ext2_object_t *o, off_t offs, char *data, size_t len, int *status);


extern int ext2_read(ext2_fs_info_t *f, id_t *id, off_t offs, char *data, size_t len, int *status);


extern int ext2_write_unlocked(ext2_fs_info_t *f, id_t *id, off_t offs, const char *data, size_t len, int *status);


extern int ext2_write(ext2_fs_info_t *f, id_t *id, off_t offs, const char *data, size_t len, int *status);


extern int ext2_truncate(ext2_fs_info_t *f, id_t *id, size_t size);


#endif /* file.h */
