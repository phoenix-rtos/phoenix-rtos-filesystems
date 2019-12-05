/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * dir.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DIR_H_
#define _DIR_H_ /* dir.h */


extern int dir_is_empty(ext2_object_t *dir);


extern int dir_find(ext2_object_t *dir, const char *name, uint32_t len, id_t *resId);


extern int dir_add(ext2_object_t *dir, const char *name, size_t len, uint16_t type, id_t *id);


extern int dir_remove(ext2_object_t *dir, const char *name, size_t len);

#endif /* dir.h */
