/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk, Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DUMMYFS_DIR_H_
#define _DUMMYFS_DIR_H_

#include "dummyfs.h"


int dir_findId(const dummyfs_object_t *dir, const char *name, const size_t len, id_t *resId, mode_t *mode);


int dir_replace(dummyfs_object_t *dir, const char *name, const size_t len, const dummyfs_object_t *o);


int dir_add(dummyfs_object_t *dir, const char *name, const size_t len, const dummyfs_object_t *o);


int dir_remove(dummyfs_object_t *dir, const char *name, const size_t len);


int dir_empty(const dummyfs_object_t *dir);


void dir_clean(dummyfs_object_t *dir);


void dir_destroy(dummyfs_object_t *dir);

#endif /* _DUMMYFS_DIR_H_ */
