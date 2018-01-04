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


static int dummyfs_truncate(vnode_t *v, unsigned int size)
{
	dummyfs_entry_t *entry;
	dummyfs_chunk_t *chunk;
	dummyfs_filedesc_t *fd;

	if (v == NULL)
		return -EINVAL;

	if (v->type != vnodeFile)
		return -EINVAL;
	if (v->size == size)
		return EOK;

	entry = v->fs_priv;
	assert(entry != NULL);
	fd = &entry->filedes;

	if (size == fd->size)
		return EOK;
		
	if (size > fd->size) {

		/* expansion */
		if(fd->first == NULL) {
			/* allocate new chunk */
			unsigned allocSize = (size < DUMMYFS_MIN_ALLOC) ? DUMMYFS_MIN_ALLOC : size;
			if(!CHECK_MEMAVAL(sizeof(dummyfs_chunk_t)))
				return -ENOMEM;

			if((fd->first=vm_kmalloc(sizeof(dummyfs_chunk_t)))==NULL) {
				MEM_RELEASE(sizeof(dummyfs_chunk_t));
				return -ENOMEM;
			}
			memset(fd->first, 0x0, sizeof(dummyfs_chunk_t));
			fd->last = fd->first;
			fd->recent = fd->first;
			fd->first->next = fd->first;
			fd->first->prev = fd->first;
			if(!CHECK_MEMAVAL(allocSize))
				return -ENOMEM;
			if((fd->first->data = vm_kmalloc(allocSize))==NULL) {
				MEM_RELEASE(allocSize);
				return -ENOMEM;
			}
			fd->first->size=allocSize;
			fd->first->used=size;
			memset(fd->first->data, 0x0, size);
		}
		else {
			/* reallocate last chunk */
			if(!CHECK_MEMAVAL(size - (fd->last->offs + fd->last->size)))
				return -ENOMEM;
			char *n = vm_krealloc(fd->last->data, size - fd->last->offs);
			if(n==NULL) {
				MEM_RELEASE(size - (fd->last->offs + fd->last->size));
				return -ENOMEM;
			}
			fd->last->data = n;
			fd->last->size = size - fd->last->offs;
			memset(fd->last->data + fd->last->used, 0x0, fd->last->size - fd->last->used);
			fd->last->used = fd->last->size;
		}
	}
	else {
		/* shrink */
		dummyfs_chunk_t *toDel;
		for (chunk = fd->last; chunk != fd->first && chunk->offs >= size; chunk = chunk->prev);

		/* chunk now points to last area that shuold be preserved - everything after it will be freed. */
		toDel = chunk->next;
		while(toDel != fd->first) {
			dummyfs_chunk_t *tmp=toDel->next;
			vm_kfree(toDel->data);
			MEM_RELEASE(toDel->size);
			vm_kfree(toDel);
			MEM_RELEASE(sizeof(dummyfs_chunk_t));
			toDel = tmp;
		}
		fd->last = chunk;
		fd->first->prev = chunk;
		chunk->next = fd->first;
		fd->recent = chunk;
		chunk->used = size-chunk->offs;
	}
	fd->size = size;
	v->size = size;
	return EOK;
}


int dummyfs_read(file_t* file, offs_t offs, char *buff, unsigned int len)
{  
	dummyfs_entry_t *entry;
	dummyfs_chunk_t *chunk;
	dummyfs_filedesc_t *fd;

	int ret = 0;
	if (file == NULL || file->priv == NULL)
		return -EINVAL;
	if (file->vnode->type != vnodeFile)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	entry = file->priv;
	assert(entry != NULL);
	fd = &entry->filedes;

	if(fd->last == NULL || offs >= (fd->last->offs + fd->last->used)) {
		return 0;
	}

	for(chunk = fd->first; chunk->next != fd->first; chunk = chunk->next)
		if(chunk->offs <= offs && (chunk->offs + chunk->used) > offs )
			break;
	if(chunk->offs <= offs && (chunk->offs + chunk->size) > offs )
		do {
			int remaining = chunk->used - (offs-chunk->offs);
			assert(chunk->used > (offs-chunk->offs));
			remaining = (remaining > len) ? len : remaining;
			if(remaining > 0)
				memcpy(buff, chunk->data + (offs-chunk->offs), remaining);

			buff += remaining;
			len -= remaining;
			offs+=remaining;
			ret += remaining;
			fd->recent = chunk;
			chunk = chunk->next;
		}while(len > 0 && chunk != fd->first);
	else
		ret = 0;
	return ret;
}


int dummyfs_write(file_t* file, offs_t offs, char *buff, unsigned int len)
{
	vnode_t *vnode;
	dummyfs_entry_t *entry;
	dummyfs_chunk_t *chunk;
	dummyfs_filedesc_t *fd;

	int ret = 0;
	unsigned int allocSize = (len < DUMMYFS_MIN_ALLOC) ? DUMMYFS_MIN_ALLOC : len;

	if (file == NULL || file->priv == NULL || file->vnode == NULL)
		return -EINVAL;
	if (file->vnode->type != vnodeFile)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	entry = file->priv;
	vnode = file->vnode;
	assert(entry != NULL);
	fd = &entry->filedes;

	if(fd->first == NULL) {
		if(!CHECK_MEMAVAL(sizeof(dummyfs_chunk_t)))
			return -ENOMEM;
		if((fd->first = vm_kmalloc(sizeof(dummyfs_chunk_t)))==NULL) {
			MEM_RELEASE(sizeof(dummyfs_chunk_t));
			return -ENOMEM;
		}
		chunk=fd->first;
		fd->last = chunk;
		fd->recent = chunk;
		chunk->next = chunk;
		chunk->prev = chunk;

		memset(chunk, 0x0, sizeof(dummyfs_chunk_t));
		if(!CHECK_MEMAVAL(allocSize))
			return -ENOMEM;/* no need to free entry, because it is a first entry */
		if((chunk->data = vm_kmalloc(allocSize))==NULL) {
			MEM_RELEASE(allocSize);
			return -ENOMEM;
		}
		chunk->size = allocSize;
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
