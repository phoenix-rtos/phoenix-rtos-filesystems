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

#include "dummyfs_internal.h"

extern int dir_find(dummyfs_object_t *dir, const char *name, oid_t *res);


extern int dir_replace(dummyfs_object_t *dir, const char *name, oid_t *new);


extern int dir_add(dummyfs_t *ctx, dummyfs_object_t *dir, const char *name, uint32_t mode, oid_t *oid);


extern int dir_remove(dummyfs_t *ctx, dummyfs_object_t *dir, const char *name);


extern int dir_empty(dummyfs_t *ctx, dummyfs_object_t *dir);


void dir_clean(dummyfs_t *ctx, dummyfs_object_t *dir);


extern void dir_destroy(dummyfs_t *ctx, dummyfs_object_t *dir);

#endif /* _DUMMYFS_DIR_H_ */
