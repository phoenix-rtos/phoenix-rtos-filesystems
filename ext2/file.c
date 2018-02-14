/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * file.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pc-ata.h>

#include "ext2.h"
#include "file.h"
#include "object.h"
#include "block.h"
#include "sb.h"
#include "inode.h"


/* reads a file */
static int _ext2_read(oid_t *oid, offs_t offs, char *data, unsigned int len, int lock)
{
	u32 read_len, read_sz, current_block, end_block;
	u32 start_block = offs / ext2->block_size;
	u32 block_off = offs % ext2->block_size; /* block offset */
	u32 off[4] = { 0 }; /* indirection level offsets */
	u32 prev_off[4] = {2048, 2048, 2048, 2048}; /* max is 1024 for 4kB blocks */
	ext2_object_t *o = object_get(oid->id);
	u32 *ind[3]; /* indirection blocks */
	void *tmp;

	if (len == 0)
		return 0;

	if (o->inode->size <= offs)
		return EOK;

	if (len > o->inode->size - offs)
		read_len = o->inode->size - offs;
	else
		read_len = len;

	ind[0] = malloc(ext2->block_size);
	ind[1] = malloc(ext2->block_size);
	ind[2] = malloc(ext2->block_size);

	current_block = start_block + 1;
	end_block = (offs + read_len) / ext2->block_size;

	tmp = malloc(ext2->block_size);

	if (lock) mutexLock(o->lock);

	get_block(o->inode, start_block, tmp, off, prev_off, ind);

	read_sz = ext2->block_size - block_off > read_len ?
		read_len : ext2->block_size - block_off;

	memcpy(data, tmp + block_off, read_sz);

	while (current_block < end_block) {
		get_block(o->inode, current_block, data + read_sz, off, prev_off, ind);
		current_block++;
		read_sz += ext2->block_size;
	}

	if (start_block != end_block && read_len > read_sz) {
		get_block(o->inode, end_block, tmp, off, prev_off, ind);
		memcpy(data + read_sz, tmp, read_len - read_sz);
	}

	if (lock) mutexUnlock(o->lock);

	object_put(o);
	free(ind[0]);
	free(ind[1]);
	free(ind[2]);
	free(tmp);
	return read_len;
}


int ext2_read(oid_t *oid, offs_t offs, char *data, u32 len)
{
	return _ext2_read(oid, offs, data, len, 1);
}


int ext2_read_locked(oid_t *oid, offs_t offs, char *data, u32 len)
{
	return _ext2_read(oid, offs, data, len, 0);
}


/* writes a file */
static int _ext2_write(oid_t *oid, offs_t offs, char *data, u32 len, int lock)
{
	u32 write_len, write_sz, current_block, end_block;
	u32 start_block = offs / ext2->block_size;
	u32 block_off = offs % ext2->block_size; /* block offset */
	u32 off[4] = { 0 }; /* indirection level offsets */
	u32 prev_off[4] = {2048, 2048, 2048, 2048}; /* max is 1024 for 4kB blocks */
	ext2_object_t *o = object_get(oid->id);
	u32 *ind[3]; /* indirection blocks */
	void *tmp;

	if (len == 0) {
		object_put(o);
		return EOK;
	}

	if (o->locked) {
		object_put(o);
		return -EINVAL;
	}

	if (!o->inode->links_count) {
		object_put(o);
		return -EINVAL;
	}

	write_len = len;

	ind[0] = malloc(ext2->block_size);
	ind[1] = malloc(ext2->block_size);
	ind[2] = malloc(ext2->block_size);

	current_block = start_block + 1;
	end_block = (offs + write_len) / ext2->block_size;

	tmp = malloc(ext2->block_size);

	if (lock) mutexLock(o->lock);

	get_block(o->inode, start_block, tmp, off, prev_off, ind);

	write_sz = ext2->block_size - block_off > write_len ?
		write_len : ext2->block_size - block_off;

	memcpy(tmp + block_off, data, write_sz);

	set_block(o->oid.id, o->inode, start_block, tmp, off, prev_off, ind);

	while (current_block < end_block) {
		set_block(o->oid.id, o->inode, current_block, data + write_sz, off, prev_off, ind);
		current_block++;
		write_sz += ext2->block_size;
	}

	if (start_block != end_block && write_len > write_sz) {
		get_block(o->inode, end_block, tmp, off, prev_off, ind);
		memcpy(tmp, data + write_sz, write_len - write_sz);
		set_block(o->oid.id, o->inode, end_block, tmp, off, prev_off, ind);
	}


	if (offs > o->inode->size)
		o->inode->size += (offs - o->inode->size) + len;
	else if (offs + len > o->inode->size)
		o->inode->size += (offs + len) - o->inode->size;

	inode_set(o->oid.id, o->inode);

	if (lock) mutexUnlock(o->lock);

	object_put(o);
	free(ind[0]);
	free(ind[1]);
	free(ind[2]);
	free(tmp);
	ext2_write_sb();
	return write_len;
}


int ext2_write(oid_t *oid, offs_t offs, char *data, u32 len)
{
	return _ext2_write(oid, offs, data, len, 1);
}


int ext2_write_locked(oid_t *oid, offs_t offs, char *data, u32 len)
{
	return _ext2_write(oid, offs, data, len, 0);
}


int ext2_truncate(oid_t *oid, u32 size)
{
	ext2_object_t *o = object_get(oid->id);
	u32 target_block = size / ext2->block_size;
	u32 end_block = o->inode->size / ext2->block_size;
	u32 *ind[3];

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	if (o->inode->size > size) {
		ind[0] = malloc(ext2->block_size);
		ind[1] = malloc(ext2->block_size);
		ind[2] = malloc(ext2->block_size);

		while (target_block < end_block) {

			free_inode_block(o->inode, end_block, ind);
			o->inode->blocks -= ext2->block_size / 512;

			end_block--;
		}

		if (!size) {
			o->inode->blocks = 0;
			free_inode_block(o->inode, 0, ind);
		}
	}

	o->inode->size = size;

	inode_set(o->oid.id, o->inode);
	mutexUnlock(o->lock);
	object_put(o);
	ext2_write_sb();
	free(ind[0]);
	free(ind[1]);
	free(ind[2]);
	return EOK;
}

