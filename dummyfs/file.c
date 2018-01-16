/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Dummy filesystem - file operations
 *
 * Copyright 2012, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>

#include "dummyfs.h"


int dummyfs_truncate(dummyfs_object_t *o, unsigned int size)
{
	dummyfs_chunk_t *chunk, *trash;
	char *tmp = NULL;
	unsigned int chunksz;

	if (o == NULL)
		return -EINVAL;

	if (o->type != otFile)
		return -EINVAL;

	if (o->size == size)
		return EOK;

	chunk = o->chunks;

	if (size > o->size) {

		/* expansion */
		if (chunk == NULL) {
			/* allocate new chunk */
			if (dummyfs_cksz(sizeof(dummyfs_chunk_t) != EOK))
					return -ENOMEM;
			chunk = malloc(sizeof(dummyfs_chunk_t));

			chunk->offs = 0;
			chunk->size = size;
			chunk->used = 0;
			chunk->data = NULL;
			chunk->next = chunk;
			chunk->prev = chunk;
			o->chunks = chunk;
			dummyfs_incsz(sizeof(dummyfs_chunk_t));
		}
		else {
			/* reallocate last chunk or alloc new one if reallocation fails */
			chunk->prev->size += size - o->size;
			if (chunk->prev->used) {
				if (dummyfs_cksz(size - o->size) != EOK)
					return -ENOMEM;

				tmp = realloc(chunk->prev->data, chunk->prev->size);
				if (tmp == NULL) {
					chunk->prev->size -= size - o->size;

					if (dummyfs_cksz(sizeof(dummyfs_chunk_t) != EOK))
						return -ENOMEM;

					chunk = malloc(sizeof(dummyfs_chunk_t));

					if (chunk == NULL)
						return -ENOMEM;
					dummyfs_incsz(sizeof(dummyfs_chunk_t));

					chunk->offs = o->size;
					chunk->size = size - o->size;
					chunk->used = 0;
					chunk->data = NULL;
					chunk->next = o->chunks;
					chunk->prev = o->chunks->prev;
					o->chunks->prev->next = chunk;
					o->chunks->prev = chunk;
				} else {
					chunk->prev->data = tmp;
					dummyfs_incsz(size + o->size);
				}
			}
		}
	}
	else {
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
			free(trash->data);
			free(trash);
		}
	}
	o->size = size;
	return EOK;
}


int dummyfs_read(dummyfs_object_t *o, offs_t offs, char *buff, unsigned int len)
{
	int ret = EOK;
	int readsz;
	int readoffs;
	dummyfs_chunk_t *chunk;

	if (o == NULL)
		return -EINVAL;

	if (o->type != otFile)
		return -EINVAL;

	if (buff == NULL)
		return -EINVAL;

	if (o->size <= offs)
		return -EINVAL;

	if (len == 0)
		return EOK;

	for(chunk = o->chunks; chunk->next != o->chunks; chunk = chunk->next) {
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

	return ret;
}


int dummyfs_write(dummyfs_object_t *o, offs_t offs, char *buff, unsigned int len)
{
	dummyfs_chunk_t *chunk;
	int writesz, writeoffs;
	int ret = EOK;

	if (o == NULL)
		return -EINVAL;

	if (o->type != otFile)
		return -EINVAL;

	if (buff == NULL)
		return -EINVAL;

	if (len == 0)
		return EOK;

	if (offs + len > o->size)
		if ((ret = dummyfs_truncate(o, offs + len)) != EOK) {
			return ret;
		}

	for (chunk = o->chunks; chunk->next != o->chunks; chunk = chunk->next)
		if ((chunk->offs + chunk->size) > offs)
			break; /* found appropriate chunk */

	ret = 0;
	do {
		writeoffs = offs - chunk->offs;
		writesz = len > chunk->size - writeoffs ? chunk->size - writeoffs : len;

		if (!chunk->used) {
			if (dummyfs_cksz(chunk->size) != EOK)
				return -ENOMEM;
			chunk->data = malloc(chunk->size);
			memset(chunk->data, 0, writeoffs);
			memset(chunk->data + writeoffs +  writesz, 0, chunk->size - writesz - writeoffs);
			chunk->used = writesz;
			dummyfs_incsz(chunk->size);
		} else
			chunk->used += writesz;
		memcpy(chunk->data + writeoffs, buff, writesz);

		len  -= writesz;
		offs += writesz;
		buff += writesz;
		ret  += writesz;

		chunk = chunk->next;

	} while (len && chunk != o->chunks);

	return ret;
}
