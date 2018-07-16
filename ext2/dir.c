/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * dir.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */
#include <stdlib.h>
#include <string.h>
#include <sys/threads.h>
#include <errno.h>

#include "ext2.h"
#include "block.h"
#include "file.h"

/* search dir for a given file name */
int dir_find(ext2_object_t *d, const char *name, u32 len, oid_t *res)
{
	u32 offs = 0;
	void *data;

	if (!(d->inode->mode & EXT2_S_IFDIR))
		return -ENOTDIR;

	data = malloc(ext2->block_size);

	while (offs < d->inode->size) {
		ext2_read_locked(&d->oid, offs, data, ext2->block_size);
		offs += ext2->block_size;
		res->id = search_block(data, name, len);
		if (res->id)
			break;
	}

	free(data);
	return res->id ? EOK : -ENOENT;
}


int dir_add(ext2_object_t *d, const char *name, int type, oid_t *oid)
{

	u32	rec_len = 0;
	u32 offs = 0;
	void *data = NULL;
	ext2_dir_entry_t *dentry;

	/* dir entry size is always rounded to block size
	 * and we need only last block of entries */
	data = malloc(ext2->block_size);

	ext2_read_locked(&d->oid, d->inode->size ? d->inode->size - ext2->block_size : 0, data, ext2->block_size);

	while (offs < ext2->block_size) {
		dentry = data + offs;
		if(!dentry->rec_len)
			break;

		if (dentry->rec_len + offs == ext2->block_size) {
			dentry->rec_len = dentry->name_len + sizeof(ext2_dir_entry_t);

			dentry->rec_len = (dentry->rec_len + 3) & ~3;

			offs += dentry->rec_len;
			rec_len = strlen(name) + sizeof(ext2_dir_entry_t);

			rec_len = (rec_len + 3) & ~3;

			if (rec_len >= ext2->block_size - offs) {
				dentry->rec_len += ext2->block_size - offs;
				offs = ext2->block_size;
			} else rec_len = ext2->block_size - offs;

			break;
		}
		offs += dentry->rec_len;
	}

	/* no space in this block */
	if (offs >= ext2->block_size) {
		/* block alloc */
		d->inode->size += ext2->block_size;
		offs = 0;
		memset(data, 0, ext2->block_size);
	}

	dentry = data + offs;
	memcpy(dentry->name, name, strlen(name));
	dentry->name_len = strlen(name);

	if (type & EXT2_S_IFDIR)
		dentry->file_type = EXT2_FT_DIR;
	else
		dentry->file_type = EXT2_FT_REG_FILE;

	dentry->rec_len = rec_len ? rec_len : ext2->block_size;
	dentry->inode = oid->id;

	ext2_write_locked(&d->oid, d->inode->size ? d->inode->size - ext2->block_size : 0, data, ext2->block_size);

	free(data);
	return EOK;
}


int dir_remove(ext2_object_t *d, const char *name)
{
	u32 offs = 0;
	u32 block_offs;
	u32 prev_offs = 0;
	ext2_dir_entry_t *dentry, *dtemp;
	void *data = malloc(ext2->block_size);

	while (offs < d->inode->size) {
		ext2_read_locked(&d->oid, offs, data, ext2->block_size);
		block_offs = 0;
		while (block_offs < ext2->block_size) {
			dentry = data + block_offs;

			if (strlen(name) == dentry->name_len
				&& !strncmp(name, dentry->name, dentry->name_len)) {
				break;
			}
			prev_offs = block_offs;
			block_offs += dentry->rec_len;
		}
		offs += ext2->block_size;
	}

	if (offs >= d->inode->size) {
		free(data);
		return -ENOENT;
	}

	/* entry at the start of the block */
	if (!block_offs) {
		/* last entry in directory */
		if (dentry->rec_len == ext2->block_size) {
			/* free last block and adjust inode size */
			d->inode->size -= ext2->block_size;
			ext2_truncate(&d->oid, d->inode->size);
			free(data);
			return EOK;
		} else {
			/* move next dentry to the start of the block */
			dtemp = data + dentry->rec_len;
			dentry->name_len = dtemp->name_len;
			dentry->rec_len = dtemp->rec_len;
			dentry->file_type = dtemp->file_type;
			dentry->inode = dtemp->inode;
			memcpy(dentry->name, dtemp->name, dtemp->name_len);
		}
	} else {
		/* just add the rec_len to the previous dentry */
		((ext2_dir_entry_t *)(data + prev_offs))->rec_len += dentry->rec_len;
	}

	ext2_write_locked(&d->oid, offs, data, ext2->block_size);
	free(data);
	return EOK;
}

int dir_is_empty(ext2_object_t *d)
{

	u32 offs = 0;
	void *data = NULL;
	ext2_dir_entry_t *dentry;

	if (!d->inode->size)
		return EOK;

	if (d->inode->size > ext2->block_size)
		return -EBUSY;

	mutexLock(d->lock);

	data = malloc(ext2->block_size);

	ext2_read_locked(&d->oid, 0, data, ext2->block_size);

	dentry = data;

	if (strncmp(dentry->name, ".", dentry->name_len) || dentry->name_len != 1) {
		free(data);
		mutexUnlock(d->lock);
		return -EINVAL;
	}

	offs += dentry->rec_len;
	dentry = data + offs;

	if (strncmp(dentry->name, "..", dentry->name_len) || dentry->name_len != 2) {
		free(data);
		mutexUnlock(d->lock);
		return -EINVAL;
	}

	if (dentry->rec_len + offs == ext2->block_size) {
		free(data);
		mutexUnlock(d->lock);
		return EOK;
	}

	free(data);
	mutexUnlock(d->lock);
	return -EINVAL;
}
