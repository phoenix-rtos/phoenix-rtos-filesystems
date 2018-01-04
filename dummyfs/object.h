/*
 * Phoenix-RTOS
 *
 * Operating system kernel
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

#include "dummyfs.h"


extern dummyfs_object_t *object_create(dummyfs_object_t *objects, unsigned int *id);


extern dummyfs_object_t *object_get(unsigned int id);


extern void object_put(dummyfs_object_t *o);


extern int object_destroy(dummyfs_object_t *o);


void object_init(void);


#endif
