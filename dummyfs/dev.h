/*
 * Phoenix-RTOS
 *
 * Operating system kernel
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
#include "dummyfs.h"
#include "object.h"

extern dummyfs_object_t *dev_find(oid_t *oid, int create);


extern dummyfs_object_t *dev_destroy(oid_t *oid);


extern void dev_init(void);

#endif
