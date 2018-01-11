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
	char *tmp;
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
			chunk = malloc(sizeof(dummyfs_chunk_t));

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
				tmp = realloc(chunk->prev->data, chunk->prev->size);
				if (tmp == NULL) {
					chunk->prev->size -= size - o->size;
					chunk = malloc(sizeof(dummyfs_chunk_t));
					if (chunk == NULL)
						return -ENOMEM;
					chunk->offs = o->size;
					chunk->size = size - o->size;
					chunk->used = 0;
					chunk->data = NULL;
					chunk->next = o->chunks;
					chunk->prev = o->chunks->prev;
					o->chunks->prev->next = chunk;
					o->chunks->prev = chunk;
				} else
					chunk->prev->data = tmp;
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
			free(trash->data);
			free(trash);
			trash = chunk;
		}
		if (size == 0) {
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
	dummyfs_chunk_t *chunk, *split;
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

		/* check if chunk split could save some space */
		if (!chunk->used) {
			if (writeoffs > sizeof(dummyfs_chunk_t)) {
				split = malloc(sizeof(dummyfs_chunk_t));
				split->data = NULL;
				split->used = 0;
				split->size = writeoffs;
				split->offs = chunk->offs;
				split->prev = chunk->prev;
				split->next = chunk;
				chunk->prev->next = split;
				chunk->prev = split;
				chunk->offs += writeoffs;
				chunk->size -= writeoffs;
				writeoffs = 0;
				if (chunk == o->chunks)
					o->chunks = split;
			}
			if (writesz < chunk->size - writeoffs) {
				if(chunk->size - writeoffs - writesz > sizeof(dummyfs_chunk_t)) {
					split = malloc(sizeof(dummyfs_chunk_t));
					split->data = NULL;
					split->used = 0;
					split->size = chunk->size - writeoffs - writesz;
					split->offs = chunk->offs + writesz;
					split->prev = chunk;
					split->next = chunk->next;
					chunk->next->prev = split;
					chunk->next = split;
					chunk->size -= split->size;
				}
			}
			chunk->data = malloc(chunk->size);
			memset(chunk->data + writesz, 0, chunk->size - writesz);
		}

		memcpy(chunk->data + writeoffs, buff, writesz);

		chunk->used += writesz;
		len  -= writesz;
		offs += writesz;
		buff += writesz;
		ret  += writesz;

		chunk = chunk->next;

	} while (len && chunk != o->chunks);

	return ret;
}
