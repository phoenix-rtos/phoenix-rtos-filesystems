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

extern dummyfs_object_t *object_create(void);


extern dummyfs_object_t *object_get(unsigned int id);


extern void object_put(dummyfs_object_t *o);


extern int object_remove(dummyfs_object_t *o);


extern void object_lock(dummyfs_object_t *o);


extern void object_unlock(dummyfs_object_t *o);


extern void object_init(void);


#endif
