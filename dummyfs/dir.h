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

#ifndef DUMMYFS_DIR_H_
#define DUMMYFS_DIR_H_


#include "dummyfs_internal.h"


int dummyfs_dir_find(dummyfs_object_t *dir, const char *name, oid_t *res);


int dummyfs_dir_replace(dummyfs_object_t *dir, const char *name, oid_t *new);


int dummyfs_dir_add(dummyfs_t *ctx, dummyfs_object_t *dir, const char *name, uint32_t mode, oid_t *oid);


int dummyfs_dir_remove(dummyfs_t *ctx, dummyfs_object_t *dir, const char *name);


int dummyfs_dir_empty(dummyfs_t *ctx, dummyfs_object_t *dir);


void dummyfs_dir_destroy(dummyfs_t *ctx, dummyfs_object_t *dir);


int dummyfs_dir_init(dummyfs_t *ctx, dummyfs_object_t *dir);


#endif /* DUMMYFS_DIR_H_ */
