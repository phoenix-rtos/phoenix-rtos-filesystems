/*
 * Phoenix-RTOS
 *
 * dummyfs - memory management
 *
 * Copyright 2023 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef DUMMYFS_MEMORY_H_
#define DUMMYFS_MEMORY_H_


#include "dummyfs_internal.h"


#define DUMMYFS_CHUNKSZ          _PAGE_SIZE
#define DUMMYFS_CHUNKCNT(size)   (((size) + DUMMYFS_CHUNKSZ - 1) / DUMMYFS_CHUNKSZ)
#define DUMMYFS_CHUNKIDX(offset) ((offset) / DUMMYFS_CHUNKSZ)


void *dummyfs_malloc(dummyfs_t *ctx, size_t size);


void *dummyfs_calloc(dummyfs_t *ctx, size_t size);


char *dummyfs_strdup(dummyfs_t *ctx, const char *str, size_t *len);


void *dummyfs_realloc(dummyfs_t *ctx, void *ptr, size_t osize, size_t nsize);


void dummyfs_free(dummyfs_t *ctx, void *ptr, size_t size);


void *dummyfs_mmap(dummyfs_t *ctx);


void dummyfs_munmap(dummyfs_t *ctx, void *ptr);


#endif /* DUMMYFS_MEMORY_H_ */
