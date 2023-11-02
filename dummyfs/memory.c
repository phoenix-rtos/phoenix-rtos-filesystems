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

#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include "memory.h"


#if DUMMYFS_CHUNKSZ % _PAGE_SIZE != 0
#error DUMMYFS_CHUNKSZ have to be a multiple of _PAGE_SIZE
#endif


void *dummyfs_malloc(dummyfs_t *ctx, size_t size)
{
	TRACE();
	void *ptr = NULL;

	if ((ctx->size + size) <= DUMMYFS_SIZE_MAX) {
		ptr = malloc(size);
		if (ptr != NULL) {
			ctx->size += size;
		}
	}

	return ptr;
}


void *dummyfs_calloc(dummyfs_t *ctx, size_t size)
{
	TRACE();
	void *ptr = NULL;

	if ((ctx->size + size) <= DUMMYFS_SIZE_MAX) {
		ptr = calloc(1, size);
		if (ptr != NULL) {
			ctx->size += size;
		}
	}

	return ptr;
}


char *dummyfs_strdup(dummyfs_t *ctx, const char *str, size_t *len)
{
	TRACE();
	size_t size = strlen(str) + 1;
	void *ptr = dummyfs_malloc(ctx, size);

	if (ptr != NULL) {
		memcpy(ptr, str, size);
		if (len != NULL) {
			*len = size - 1;
		}
	}

	return ptr;
}


void dummyfs_free(dummyfs_t *ctx, void *ptr, size_t size)
{
	TRACE();
	if (ptr != NULL) {
		assert(ctx->size >= size);
		ctx->size -= size;
	}
	free(ptr);
}


void *dummyfs_realloc(dummyfs_t *ctx, void *ptr, size_t osize, size_t nsize)
{
	TRACE();
	if (nsize == 0) {
		dummyfs_free(ctx, ptr, osize);
		return NULL;
	}

	if ((ctx->size - osize + nsize) > DUMMYFS_SIZE_MAX) {
		return NULL;
	}

	void *tptr = realloc(ptr, nsize);
	if ((nsize > osize) && (tptr != NULL)) {
		ctx->size += nsize - osize;
	}

	if (osize > nsize) {
		assert(ctx->size >= osize - nsize);
		ctx->size -= osize - nsize;
	}

	return tptr;
}


void *dummyfs_mmap(dummyfs_t *ctx)
{
	TRACE();
	void *ptr = NULL;

	if ((ctx->size + DUMMYFS_CHUNKSZ) <= DUMMYFS_SIZE_MAX) {
		ptr = mmap(NULL, DUMMYFS_CHUNKSZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
		if (ptr != MAP_FAILED) {
			ctx->size += DUMMYFS_CHUNKSZ;
		}
		else {
			ptr = NULL;
		}
	}

	return ptr;
}


void dummyfs_munmap(dummyfs_t *ctx, void *ptr)
{
	TRACE();
	assert(ctx->size >= DUMMYFS_CHUNKSZ);
	munmap(ptr, DUMMYFS_CHUNKSZ);
	ctx->size -= DUMMYFS_CHUNKSZ;
}
