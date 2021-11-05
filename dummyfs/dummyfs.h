/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2021 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */
#ifndef _DUMMYFS_H_
#define _DUMMYFS_H_

#include "dummyfs_internal.h"


int dummyfs_open(dummyfs_t *ctx, oid_t *oid);


int dummyfs_close(dummyfs_t *ctx, oid_t *oid);


int dummyfs_read(dummyfs_t *ctx, oid_t *oid, offs_t offs, char *buff, size_t len);


int dummyfs_write(dummyfs_t *ctx, oid_t *oid, offs_t offs, const char *buff, size_t len);


int dummyfs_truncate(dummyfs_t *ctx, oid_t *oid, size_t size);


int dummyfs_create(dummyfs_t *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev);


int dummyfs_destroy(dummyfs_t *ctx, oid_t *oid);


int dummyfs_setattr(dummyfs_t *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size);


int dummyfs_getattr(dummyfs_t *ctx, oid_t *oid, int type, long long *attr);


int dummyfs_lookup(dummyfs_t *ctx, oid_t *dir, const char *name, oid_t *res, oid_t *dev);


int dummyfs_link(dummyfs_t *ctx, oid_t *dir, const char *name, oid_t *oid);


int dummyfs_unlink(dummyfs_t *ctx, oid_t *dir, const char *name);


int dummyfs_readdir(dummyfs_t *ctx, oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size);

#endif /* _DUMMYFS_H_ */