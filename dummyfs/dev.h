/*
 * Phoenix-RTOS
 *
 * dummyfs - devices
 *
 * Copyright 2018 Phoenix Systems
 * Author: Jan Sikorski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DUMMYFS_DEV_H_
#define _DUMMYFS_DEV_H_

#include <sys/msg.h>
#include "dummyfs_internal.h"
#include "object.h"

extern dummyfs_object_t *dev_find(dummyfs_t *ctx, oid_t *oid, unsigned long ino, int create);


extern int dev_destroy(dummyfs_t *ctx, oid_t *oid);


extern int dev_init(dummyfs_t *ctx);


extern void dev_cleanup(dummyfs_t *ctx);


#endif
