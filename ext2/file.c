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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>

#include "pc-ata.h"
#include "ext2.h"
#include "file.h"
#include "object.h"
#include "block.h"
#include "sb.h"
#include "inode.h"


/* reads a file */
static int _ext2_read(oid_t *oid, offs_t offs, char *data, unsigned int len, int lock)
{
	uint32_t read_len, read_sz, current_block, end_block;
	uint32_t start_block = offs / ext2->block_size;
	uint32_t block_off = offs % ext2->block_size; /* block offset */
	ext2_object_t *o = object_get(oid->id);
	void *tmp;

	if (o == NULL)
		return -EINVAL;

	if (len == 0)
		return 0;

	if (o->type == otDev) {
		object_put(o);
		return -EINVAL;
	}

	if (o->inode->size <= offs)
		return EOK;

	if (len > o->inode->size - offs)
		read_len = o->inode->size - offs;
	else
		read_len = len;

	current_block = start_block + 1;
	end_block = (offs + read_len) / ext2->block_size;

	tmp = malloc(ext2->block_size);

	if (lock) mutexLock(o->lock);

	get_block(o, start_block, tmp);

	read_sz = ext2->block_size - block_off > read_len ?
		read_len : ext2->block_size - block_off;

	memcpy(data, tmp + block_off, read_sz);

	while (current_block < end_block) {
		get_block(o, current_block, data + read_sz);
		current_block++;
		read_sz += ext2->block_size;
	}

	if (start_block != end_block && read_len > read_sz) {
		get_block(o, end_block, tmp);
		memcpy(data + read_sz, tmp, read_len - read_sz);
	}

	if (lock) mutexUnlock(o->lock);

	o->inode->atime = time(NULL);
	object_put(o);
	free(tmp);
	return read_len;
}


int ext2_read(oid_t *oid, offs_t offs, char *data, uint32_t len)
{
	return _ext2_read(oid, offs, data, len, 1);
}


int ext2_read_locked(oid_t *oid, offs_t offs, char *data, uint32_t len)
{
	return _ext2_read(oid, offs, data, len, 0);
}


/* writes a file */
static int _ext2_write(oid_t *oid, offs_t offs, char *data, uint32_t len, int lock)
{
	uint32_t write_len, write_sz, current_block, end_block;
	uint32_t start_block = offs / ext2->block_size;
	uint32_t block_off = offs % ext2->block_size; /* block offset */
	ext2_object_t *o = object_get(oid->id);
	void *tmp;

	if (o == NULL)
		return -EINVAL;

	if (len == 0) {
		object_put(o);
		return EOK;
	}

	if (o->type == otDev) {
		object_put(o);
		return -EINVAL;
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
	write_sz = 0;

	current_block = start_block;
	end_block = (offs + write_len) / ext2->block_size;

	tmp = malloc(ext2->block_size);

	if (lock) mutexLock(o->lock);

	if (block_off || write_len < ext2->block_size) {

		current_block++;
		get_block(o, start_block, tmp);

		write_sz = ext2->block_size - block_off > write_len ?
			write_len : ext2->block_size - block_off;

		memcpy(tmp + block_off, data, write_sz);
		set_block(o, start_block, tmp);
	}

	if (current_block < end_block) {
		set_blocks(o, current_block, end_block - current_block, data + write_sz);
		write_sz += ext2->block_size * (end_block - current_block);
		current_block += end_block - current_block;
	}

	if (write_len > write_sz) {
		get_block(o, end_block, tmp);
		memcpy(tmp, data + write_sz, write_len - write_sz);
		set_block(o, end_block, tmp);
	}

	if (offs > o->inode->size)
		o->inode->size += (offs - o->inode->size) + len;
	else if (offs + len > o->inode->size)
		o->inode->size += (offs + len) - o->inode->size;

	o->dirty = 1;

	if (lock) mutexUnlock(o->lock);
	o->inode->mtime = o->inode->atime = time(NULL);
	object_sync(o);
	object_put(o);
	free(tmp);
	ext2_write_sb();
	return write_len;
}


int ext2_write(oid_t *oid, offs_t offs, char *data, uint32_t len)
{
	return _ext2_write(oid, offs, data, len, 1);
}


int ext2_write_locked(oid_t *oid, offs_t offs, char *data, uint32_t len)
{
	return _ext2_write(oid, offs, data, len, 0);
}


int ext2_truncate(oid_t *oid, uint32_t size)
{
	ext2_object_t *o = object_get(oid->id);
	uint32_t target_block = size / ext2->block_size;
	uint32_t end_block = o->inode->size / ext2->block_size;
	uint32_t current, count, last = 0;

	if (o == NULL)
		return -EINVAL;

	if (o->type == otDev) {
		object_put(o);
		return -EINVAL;
	}

	mutexLock(o->lock);

	if (o->inode->size > size) {

		count = 0;
		while (target_block < end_block) {

			current = get_block_no(o, end_block);
			if (current == last - 1 || last == 0) {
				count++;
				last = current;
			} else {
				free_blocks(last, count);
				last = current;
				count = 1;
			}
			o->inode->blocks -= ext2->block_size / 512;
			end_block--;
		}

		if (last && count)
			free_blocks(last, count);

		end_block = o->inode->size / ext2->block_size;
		free_inode_blocks(o, end_block, end_block - target_block);

		if (!size) {
			o->inode->blocks = 0;
			free_blocks(get_block_no(o, 0), 1);
			free_inode_blocks(o, 0, 1);
		}
	}

	o->inode->size = size;

	o->dirty = 1;
	mutexUnlock(o->lock);
	object_sync(o);
	object_put(o);
	ext2_write_sb();
	return EOK;
}

