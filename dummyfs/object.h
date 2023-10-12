/*
 * Phoenix-RTOS
 *
 * dummyfs - object storage
 *
 * Copyright 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef DUMMYFS_OBJECT_H_
#define DUMMYFS_OBJECT_H_


#include "dummyfs_internal.h"


dummyfs_object_t *dummyfs_object_create(dummyfs_t *ctx);


dummyfs_object_t *dummyfs_object_get(dummyfs_t *ctx, oid_t *oid);


dummyfs_object_t *dummyfs_object_find(dummyfs_t *ctx, oid_t *oid);


void dummyfs_object_put(dummyfs_t *ctx, dummyfs_object_t *o);


int dummyfs_object_remove(dummyfs_t *ctx, dummyfs_object_t *o);


int dummyfs_object_init(dummyfs_t *ctx);


void dummyfs_object_cleanup(dummyfs_t *ctx);


#endif /* DUMMYFS_OBJECT_H_ */
