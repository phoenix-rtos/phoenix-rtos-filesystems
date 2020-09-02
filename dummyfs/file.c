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

#include "dummyfs.h"
#include "file.h"
#include "object.h"

int dummyfs_truncate(oid_t *oid, size_t size)
{
	dummyfs_object_t *o;
	int ret;

	o = object_get(oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode)) {
		object_put(o);
		return -EACCES;
	}

	if (o->size == size) {
		object_put(o);
		return EOK;
	}

	object_lock(o);

	ret = dummyfs_truncate_internal(o, size);

	object_unlock(o);
	object_put(o);

	return ret;
}


int dummyfs_truncate_internal(dummyfs_object_t *o, size_t size)
{
	dummyfs_chunk_t *chunk, *trash;
	char *tmp = NULL;
	unsigned int chunksz;

	chunk = o->chunks;

	if (size > o->size) {
		/* expansion */
		if (chunk == NULL) {
			/* allocate new chunk */
			if (dummyfs_incsz(sizeof(dummyfs_chunk_t)) != EOK) {
				return -ENOMEM;
			}

			chunk = malloc(sizeof(dummyfs_chunk_t));

			if (chunk == NULL) {
				dummyfs_decsz(sizeof(dummyfs_chunk_t));
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
			chunk->prev->size += size - o->size;
			if (chunk->prev->used) {
				if (dummyfs_incsz(size - o->size) != EOK) {
					return -ENOMEM;
				}

				tmp = realloc(chunk->prev->data, chunk->prev->size);
				if (tmp == NULL) {
					dummyfs_decsz(size - o->size);
					chunk->prev->size -= size - o->size;

					if (dummyfs_incsz(sizeof(dummyfs_chunk_t)) != EOK) {
						return -ENOMEM;
					}

					chunk = malloc(sizeof(dummyfs_chunk_t));

					if (chunk == NULL) {
						dummyfs_decsz(sizeof(dummyfs_chunk_t));
						return -ENOMEM;
					}

					chunk->offs = o->size;
					chunk->size = size - o->size;
					chunk->used = 0;
					chunk->data = NULL;
					chunk->next = o->chunks;
					chunk->prev = o->chunks->prev;
					o->chunks->prev->next = chunk;
					o->chunks->prev = chunk;
				}
				else {
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

		if (chunk->offs + chunk->size > size)
		{
			chunksz = size - chunk->offs;
			tmp = realloc(chunk->data, chunksz);
			if (tmp == NULL)
				return -ENOMEM;

			dummyfs_decsz(chunk->size - chunksz);
			chunk->used = chunk->used > chunksz ? chunksz : chunk->used;
			chunk->size = chunksz;
			chunk->data = tmp;
		}

		/* chunk now points to last area that shuold be preserved - everything after it will be freed. */
		trash = chunk->next;
		chunk->next = o->chunks;
		o->chunks->prev = chunk;
		while (trash != o->chunks) {
			chunk = trash->next;
			dummyfs_decsz(sizeof(dummyfs_chunk_t) + trash->size);
			free(trash->data);
			free(trash);
			trash = chunk;
		}

		if (size == 0) {
			dummyfs_decsz(sizeof(dummyfs_chunk_t) + trash->size);
			o->chunks = NULL;
			free(trash->data);
			free(trash);
		}
	}

	o->size = size;
	o->mtime = o->atime = time(NULL);

	return EOK;
}


int dummyfs_read(oid_t *oid, offs_t offs, char *buff, size_t len)
{
	int ret = EOK;
	int readsz;
	int readoffs;
	dummyfs_chunk_t *chunk;
	dummyfs_object_t *o;

	o = object_get(oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode) && !S_ISLNK(o->mode) && o->mode != 0xaBadBabe)
		ret = -EINVAL;

	if (buff == NULL)
		ret = -EINVAL;

	if (o->size <= offs) {
		object_put(o);
		return 0;
	}

	if (ret != EOK) {
		object_put(o);
		return ret;
	}

	if (len == 0) {
		object_put(o);
		return EOK;
	}

	object_lock(o);
	for (chunk = o->chunks; chunk->next != o->chunks; chunk = chunk->next) {
		if (chunk->offs + chunk->size > offs) {
			break;
		}
	}

	do {
		readoffs = offs - chunk->offs;
		readsz = len > chunk->size - readoffs ? chunk->size - readoffs : len;
		if (chunk->used)
			memcpy(buff, chunk->data + readoffs, readsz);
		else
			memset(buff, 0, readsz);

		len  -= readsz;
		buff += readsz;
		offs += readsz;
		ret  += readsz;

		chunk = chunk->next;

	} while (len && chunk != o->chunks);

	o->atime = time(NULL);
	object_unlock(o);
	object_put(o);

	return ret;
}


int dummyfs_write(oid_t *oid, offs_t offs, const char *buff, size_t len)
{
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get(oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode))
		ret = -EINVAL;

	if (buff == NULL)
		ret = -EINVAL;

	if (ret != EOK) {
		object_put(o);
		return ret;
	}

	object_lock(o);

	ret = dummyfs_write_internal(o, offs, buff, len);

	object_unlock(o);
	object_put(o);

	return ret;
}

int dummyfs_write_internal(dummyfs_object_t *o, offs_t offs, const char *buff, size_t len)
{

	int writesz, writeoffs;
	dummyfs_chunk_t *chunk;
	int ret = EOK;

	if (len == 0) {
		return EOK;
	}

	if (offs + len > o->size) {
		if ((ret = dummyfs_truncate_internal(o, offs + len)) != EOK)
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
			if (dummyfs_incsz(chunk->size) != EOK) {
				return -ENOMEM;
			}

			chunk->data = malloc(chunk->size);

			if (chunk->data == NULL) {
				dummyfs_decsz(chunk->size);
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
