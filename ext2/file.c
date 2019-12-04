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
#include <time.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/threads.h>
#include <phoenix/stat.h>
#include <atasrv.h>

#include "ext2.h"
#include "file.h"
#include "object.h"
#include "block.h"
#include "sb.h"
#include "inode.h"


extern int ext2_readdir(ext2_object_t *d, off_t offs, struct dirent *dent, unsigned int size);

/* reads a file */
int ext2_read_internal(ext2_object_t *o, off_t offs, char *data, size_t len, int *status)
{
	uint32_t read_len, read_sz, current_block, end_block;
	uint32_t start_block = offs / o->f->block_size;
	uint32_t block_off = offs % o->f->block_size; /* block offset */
	void *tmp;

	if (o == NULL)
		return -EINVAL;

	/* TODO: symlink special case */
	//else if (!S_ISREG(o->inode->mode)) {
	//	return -EINVAL;
	//}

	if (len == 0)
		return 0;

	if (o->inode->size <= offs)
		return EOK;

	if (len > o->inode->size - offs)
		read_len = o->inode->size - offs;
	else
		read_len = len;

	current_block = start_block + 1;
	end_block = (offs + read_len) / o->f->block_size;

	tmp = malloc(o->f->block_size);

	get_block(o, start_block, tmp);

	read_sz = o->f->block_size - block_off > read_len ?
		read_len : o->f->block_size - block_off;

	memcpy(data, tmp + block_off, read_sz);

	while (current_block < end_block) {
		get_block(o, current_block, data + read_sz);
		current_block++;
		read_sz += o->f->block_size;
	}

	if (start_block != end_block && read_len > read_sz) {
		get_block(o, end_block, tmp);
		memcpy(data + read_sz, tmp, read_len - read_sz);
	}

	o->inode->atime = time(NULL);
	free(tmp);
	return read_len;
}


int ext2_read(ext2_fs_info_t *f, id_t *id, off_t offs, char *data, size_t len, int *status)
{
	int ret;
	ext2_object_t *o = object_get(f, id);

	*status = EOK;

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);
	if (S_ISDIR(o->inode->mode)) {
		if (object_checkFlag(o, EXT2_FL_MOUNT) && len >= sizeof(oid_t)) {
			memcpy(data, &o->mnt, sizeof(oid_t));
			ret = sizeof(oid_t);
		}  //else if (object_checkFlag(o, EXT2_FL_MOUNTPOINT) && len >= sizeof(oid_t)) {
			//memcpy(data, &o->f->parent, sizeof(oid_t));
			//return sizeof(oid_t);
		//}
		else {
			ret = ext2_readdir(o, offs, (struct dirent *)data, len);
		}
	}
	else if ((S_ISCHR(o->inode->mode) || S_ISBLK(o->inode->mode)) && len >= sizeof(oid_t)) {
		memcpy(data, &o->inode->blocks, sizeof(oid_t));
		ret = sizeof(oid_t);
	}
	else {
		ret = ext2_read_internal(o, offs, data, len, status);
	}
	if (ret < 0) {
		*status = ret;
		ret = 0;
	}
	mutexUnlock(o->lock);
	object_put(o);
	return ret;
}


/* writes a file */
static int _ext2_write(ext2_object_t *o, offs_t offs, const char *data, size_t len, int *status)
{
	uint32_t write_len, write_sz, current_block, end_block;
	uint32_t start_block = offs / o->f->block_size;
	uint32_t block_off = offs % o->f->block_size; /* block offset */
	void *tmp;

	if (o == NULL)
		return -EINVAL;

	if (len == 0) {
		object_put(o);
		return EOK;
	}

	write_len = len;
	write_sz = 0;

	current_block = start_block;
	end_block = (offs + write_len) / o->f->block_size;

	tmp = malloc(o->f->block_size);

	if (block_off || write_len < o->f->block_size) {

		current_block++;
		get_block(o, start_block, tmp);

		write_sz = o->f->block_size - block_off > write_len ?
			write_len : o->f->block_size - block_off;

		memcpy(tmp + block_off, data, write_sz);
		set_block(o, start_block, tmp);
	}

	if (current_block < end_block) {
		set_blocks(o, current_block, end_block - current_block, data + write_sz);
		write_sz += o->f->block_size * (end_block - current_block);
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

	object_setFlag(o, EXT2_FL_DIRTY);

	o->inode->mtime = o->inode->atime = time(NULL);
	object_sync(o);
	free(tmp);
	ext2_write_sb(o->f);
	return write_len;
}


int ext2_write_unlocked(ext2_fs_info_t *f, id_t *id, off_t offs, const char *data, size_t len, int *status)
{
	int ret;
	ext2_object_t *o = object_get(f, id);
	ret = _ext2_write(o, offs, data, len, status);
	object_put(o);
	return ret;
}


int ext2_write(ext2_fs_info_t *f, id_t *id, off_t offs, const char *data, size_t len, int *status)
{
	int ret;
	ext2_object_t *o = object_get(f, id);
	mutexLock(o->lock);
	ret = _ext2_write(o, offs, data, len, status);
	mutexUnlock(o->lock);
	object_put(o);
	return ret;
}


int ext2_truncate(ext2_fs_info_t *f, id_t *id, size_t size)
{
	ext2_object_t *o = object_get(f, id);
	uint32_t target_block = size / f->block_size;
	uint32_t end_block = o->inode->size / f->block_size;
	uint32_t current, count, last = 0;

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	if (o->inode->size > size) {

		count = 0;
		while (target_block < end_block) {

			current = get_block_no(o, end_block);
			if (current == last - 1 || last == 0) {
				count++;
				last = current;
			} else {
				free_blocks(f, last, count);
				last = current;
				count = 1;
			}
			o->inode->blocks -= f->block_size / 512 /* TODO: this shouldbe sector size? */;
			end_block--;
		}

		if (last && count)
			free_blocks(f, last, count);

		end_block = o->inode->size / f->block_size;
		free_inode_blocks(o, end_block, end_block - target_block);

		if (!size) {
			o->inode->blocks = 0;
			free_blocks(f, get_block_no(o, 0), 1);
			free_inode_blocks(o, 0, 1);
		}
	}

	o->inode->size = size;

	object_setFlag(o, EXT2_FL_DIRTY);
	mutexUnlock(o->lock);
	object_sync(o);
	object_put(o);
	ext2_write_sb(f);
	return EOK;
}

