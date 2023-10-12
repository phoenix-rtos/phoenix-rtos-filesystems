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
#ifndef DUMMYFS_H_
#define DUMMYFS_H_


int dummyfs_open(void *ctx, oid_t *oid);


int dummyfs_close(void *ctx, oid_t *oid);


int dummyfs_read(void *ctx, oid_t *oid, offs_t offs, char *buff, size_t len);


int dummyfs_write(void *ctx, oid_t *oid, offs_t offs, const char *buff, size_t len);


int dummyfs_truncate(void *ctx, oid_t *oid, size_t size);


int dummyfs_create(void *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev);


int dummyfs_destroy(void *ctx, oid_t *oid);


int dummyfs_setattr(void *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size);


int dummyfs_getattr(void *ctx, oid_t *oid, int type, long long *attr);


int dummyfs_lookup(void *ctx, oid_t *dir, const char *name, oid_t *res, oid_t *dev);


int dummyfs_link(void *ctx, oid_t *dir, const char *name, oid_t *oid);


int dummyfs_unlink(void *ctx, oid_t *dir, const char *name);


int dummyfs_readdir(void *ctx, oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size);


int dummyfs_createMapped(void *ctx, oid_t *dir, const char *name, void *addr, size_t size, oid_t *oid);


int dummyfs_statfs(void *ctx, void *buf, size_t len);


int dummyfs_mount(void **ctx, const char *data, unsigned long mode, oid_t *root);


int dummyfs_unmount(void *ctx);


#endif /* DUMMYFS_H_ */
