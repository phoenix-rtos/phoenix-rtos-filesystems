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
			/* reallocate last chunk */
			chunk->prev->size += size - o->size;
			if (chunk->prev->used) {
				tmp = realloc(chunk->prev->data, chunk->prev->size);
				if (tmp == NULL) {
					chunk->prev->size -= size - o->size;
					return -ENOMEM;
				}
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
		} while (chunk != o->chunks)

		if (chunk->off + chunk->size > size)
		{
			chunksz = size - chunk->off;
			tmp = realloc(chunk->data, chunksz);
			if (tmp == NULL)
				return -ENOMEM;
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
	int ret = 0;
	int readsz;
	int readoffs;

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

	for(chunk = o->chunks; chunk != o->chunks; chunk = chunk->next)
		if (chunk->offs + chunk->size > offs)
			break;

	do {
		readsz = len > chunk->size ? chunk->size : len;
		readoffs = offs - chunk->offs;
		if (chunk->used)
			memcpy(buff, chunk->data + readoffs, chunk->size - readoffs);
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

	int ret = 0;

	if (o == NULL)
		return -EINVAL;

	if (o->type != otFile)
		return -EINVAL;

	if (buff == NULL)
		return -EINVAL;

	if (len == 0)
		return EOK;

	if (o->chunks == NULL) {
		chunk = malloc(sizeof(dummyfs_chunk_t));
		chunk->next = chunk;
		chunk->prev = chunk;
		chunk->offs = 0;
		chunk->size = offs;
		chunk->data = NULL;
		chunk->used = 0;
		o->chunks = chunk;
	}

	/* NO SUPPORT FOR SPARSE FILES */
	if(offs > (fd->last->offs + fd->last->used))
		return -EINVAL;

	if(offs == (fd->last->offs + fd->last->used))
		chunk = fd->last;/* appending */
	else
		for(chunk = fd->first; chunk->next != fd->first; chunk = chunk->next)
			if(chunk->offs <= offs && (chunk->offs + chunk->size) > offs )
				break; /* found appropriate chunk */


	do {
		int remaining = chunk->size - (offs-chunk->offs);
		int used;
		remaining = (remaining > len) ? len : remaining;
		used = (offs - chunk->offs) + remaining;
		if(remaining > 0)
			memcpy(chunk->data + (offs-chunk->offs), buff, remaining);

		buff += remaining;
		len -= remaining;
		ret += remaining;
		offs += remaining;

		chunk->used = (chunk->used > used) ? chunk->used : used;
		vnode->size = fd->last->offs + fd->last->used;
		fd->size = fd->last->offs + fd->last->used;

		if(len > 0) {
			if(chunk->next == fd->first) {
				allocSize = (len < DUMMYFS_MIN_ALLOC) ? DUMMYFS_MIN_ALLOC : len;
				dummyfs_chunk_t *n;
				if(!CHECK_MEMAVAL(sizeof(dummyfs_chunk_t)))
					return ret;
				if( (n=vm_kmalloc(sizeof(dummyfs_chunk_t)))==NULL ) {
					MEM_RELEASE(sizeof(dummyfs_chunk_t));
					return ret;
				}
				memset(n, 0x0, sizeof(dummyfs_chunk_t));
				n->offs = chunk->offs + chunk->size;
				if(!CHECK_MEMAVAL(allocSize)) {
					return ret;
				}
				if((n->data = vm_kmalloc(allocSize))==NULL) {
					MEM_RELEASE(allocSize);
					vm_kfree(n);
					MEM_RELEASE(sizeof(dummyfs_chunk_t));
					return ret;
				}
				n->size = allocSize;
				n->next = fd->first;
				n->prev = chunk;
				fd->last=n;
				chunk->next = n;
				fd->first->prev = n;
			}
			chunk = chunk->next;
		}
		fd->recent = chunk;
	}
	while(len > 0);
	assert(len == 0);

	return ret;
}
