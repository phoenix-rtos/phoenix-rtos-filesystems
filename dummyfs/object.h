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

#ifndef _DUMMYFS_OBJECT_H_
#define _DUMMYFS_OBJECT_H_


#include "dummyfs_internal.h"


extern dummyfs_object_t *object_create(dummyfs_t *ctx);


extern dummyfs_object_t *object_get(dummyfs_t *ctx, unsigned int id);


extern dummyfs_object_t *object_get_unlocked(dummyfs_t *ctx, unsigned int id);


extern void object_put(dummyfs_t *ctx, dummyfs_object_t *o);


extern int object_remove(dummyfs_t *ctx, dummyfs_object_t *o);


extern void object_lock(dummyfs_t *ctx, dummyfs_object_t *o);


extern void object_unlock(dummyfs_t *ctx, dummyfs_object_t *o);


extern int object_init(dummyfs_t *ctx);


#endif
