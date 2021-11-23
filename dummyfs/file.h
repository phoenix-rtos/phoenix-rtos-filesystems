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

#ifndef _DUMMYFS_FILE_H_
#define _DUMMYFS_FILE_H_

#include "dummyfs_internal.h"


int dummyfs_truncate_internal(dummyfs_t *ctx, dummyfs_object_t *o, size_t size);


int dummyfs_write_internal(dummyfs_t *ctx, dummyfs_object_t *o, offs_t offs, const char *buff, size_t len);


#endif /* _DUMMYFS_FILE_H_ */
