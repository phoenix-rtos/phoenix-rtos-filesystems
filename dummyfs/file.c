/*
 * Phoenix-RTOS
 *
 * Dummy filesystem - file operations
 *
 * Copyright 2012, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk, Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/threads.h>
#include <string.h>

#include "dummyfs_internal.h"
#include "file.h"
#include "object.h"


int dummyfs_truncate_internal(dummyfs_t *ctx, dummyfs_object_t *o, size_t size)
{
	dummyfs_chunk_t *chunk, *trash;
	char *tmp = NULL;
	unsigned int chunksz;
	size_t changesz;

	chunk = o->chunks;

	if (size > o->size) {
		/* expansion */
		if (chunk == NULL) {
			/* allocate new chunk */
			if (dummyfs_incsz(ctx, sizeof(dummyfs_chunk_t)) != EOK) {
				return -ENOMEM;
			}

			chunk = calloc(1, sizeof(dummyfs_chunk_t));
			if (chunk == NULL) {
				dummyfs_decsz(ctx, sizeof(dummyfs_chunk_t));
				return -ENOMEM;
			}

			chunk->offs = 0;
			chunk->size = size;
			chunk->used = 0;
			chunk->data = NULL;
			chunk->next = chunk;
			chunk->prev = chunk;
			o->chunks = chunk;
		}
		else {
			/* reallocate last chunk or alloc new one if reallocation fails */
			changesz = size - o->size;
			chunk->prev->size += changesz;
			if (chunk->prev->used) {
				if (dummyfs_incsz(ctx, changesz) != EOK) {
					return -ENOMEM;
				}

				tmp = realloc(chunk->prev->data, chunk->prev->size);
				if (tmp == NULL) {
					dummyfs_decsz(ctx, changesz);
					chunk->prev->size -= changesz;

					if (dummyfs_incsz(ctx, sizeof(dummyfs_chunk_t)) != EOK) {
						return -ENOMEM;
					}

					chunk = calloc(1, sizeof(dummyfs_chunk_t));
					if (chunk == NULL) {
						dummyfs_decsz(ctx, sizeof(dummyfs_chunk_t));
						return -ENOMEM;
					}

					chunk->offs = o->size;
					chunk->size = changesz;
					chunk->used = 0;
					chunk->data = NULL;
					chunk->next = o->chunks;
					chunk->prev = o->chunks->prev;
					o->chunks->prev->next = chunk;
					o->chunks->prev = chunk;
				}
				else {
					(void)memset(tmp + (chunk->prev->size - changesz), 0, changesz);
					chunk->prev->data = tmp;
				}
			}
		}
	}
	else if (o->chunks != NULL) {
		/* shrink */
		chunk = chunk->prev;

		do {
			if (chunk->offs >= size)
				chunk = chunk->prev;
			else
				break;
		} while (chunk != o->chunks);

		if (chunk->offs + chunk->size > size) {
			chunksz = size - chunk->offs;
			tmp = realloc(chunk->data, chunksz);
			if (chunksz > 0 && tmp == NULL)
				return -ENOMEM;

			dummyfs_decsz(ctx, chunk->size - chunksz);
			chunk->used = chunk->used > chunksz ? chunksz : chunk->used;
			chunk->size = chunksz;
			chunk->data = tmp;

			/* check if this chunk needs to also be removed; deleting first chunk (shrinking to 0) is a special case */
			if (chunksz == 0 && size != 0)
				chunk = chunk->prev;
		}

		/* chunk now points to last area that shuold be preserved - everything after it will be freed. */
		trash = chunk->next;
		chunk->next = o->chunks;
		o->chunks->prev = chunk;
		while (trash != o->chunks) {
			chunk = trash->next;
			dummyfs_decsz(ctx, sizeof(dummyfs_chunk_t) + trash->size);
			free(trash->data);
			free(trash);
			trash = chunk;
		}

		if (size == 0) {
			dummyfs_decsz(ctx, sizeof(dummyfs_chunk_t) + trash->size);
			o->chunks = NULL;
			free(trash->data);
			free(trash);
		}
	}

	o->size = size;
	o->mtime = o->atime = time(NULL);

	return EOK;
}


int dummyfs_write_internal(dummyfs_t *ctx, dummyfs_object_t *o, offs_t offs, const char *buff, size_t len)
{

	int writesz, writeoffs;
	dummyfs_chunk_t *chunk;
	int ret = EOK;

	if (len == 0) {
		return 0;
	}

	if (offs + len > o->size) {
		if ((ret = dummyfs_truncate_internal(ctx, o, offs + len)) != EOK)
			return ret;
	}

	for (chunk = o->chunks; chunk->next != o->chunks; chunk = chunk->next) {
		if ((chunk->offs + chunk->size) > offs)
			break; /* found appropriate chunk */
	}

	ret = 0;
	do {
		writeoffs = offs - chunk->offs;
		writesz = len > chunk->size - writeoffs ? chunk->size - writeoffs : len;

		if (!chunk->used) {
			if (dummyfs_incsz(ctx, chunk->size) != EOK) {
				return -ENOMEM;
			}

			chunk->data = malloc(chunk->size);

			if (chunk->data == NULL) {
				dummyfs_decsz(ctx, chunk->size);
				return -ENOMEM;
			}

			memset(chunk->data, 0, writeoffs);
			memset(chunk->data + writeoffs +  writesz, 0, chunk->size - writesz - writeoffs);
			chunk->used = writesz;
		}
		else {
			chunk->used += writesz;
		}

		memcpy(chunk->data + writeoffs, buff, writesz);

		len  -= writesz;
		offs += writesz;
		buff += writesz;
		ret  += writesz;

		chunk = chunk->next;

	} while (len && chunk != o->chunks);

	o->mtime = o->atime = time(NULL);

	return ret;
}
