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

#include "dummyfs.h"


int dummyfs_truncate(id_t *id, size_t size);


int dummyfs_truncate_internal(dummyfs_object_t *o, size_t size);


int dummyfs_read(id_t *id, offs_t offs, char *buff, size_t len, int *status);


int dummyfs_write(id_t *id, offs_t offs, const char *buff, size_t len, int *status);


int dummyfs_write_internal(dummyfs_object_t *o, offs_t offs, const char *buff, size_t len, int *status);


#endif /* _DUMMYFS_FILE_H_ */
