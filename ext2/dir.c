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
		ext2_read(&d->oid, offs, data, ext2->block_size);
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

	ext2_read(&d->oid, d->inode->size ? d->inode->size - ext2->block_size : 0, data, ext2->block_size);

	while (offs < ext2->block_size) {
		dentry = data + offs;
		if(!dentry->rec_len)
			break;
		if (dentry->rec_len + offs == ext2->block_size) {
			dentry->rec_len = dentry->name_len + sizeof(ext2_dir_entry_t);
			if (dentry->rec_len % 4)
				dentry->rec_len = (dentry->rec_len + 4) & ~3;

			offs += dentry->rec_len;
			rec_len = strlen(name) + sizeof(ext2_dir_entry_t);

			if (rec_len % 4)
				rec_len = (rec_len + 4) & ~3;

			if (rec_len >= ext2->block_size - offs) {
				offs = ext2->block_size;
				dentry->rec_len += ext2->block_size - offs;
			} else rec_len = ext2->block_size - offs;

			break;
		}
		offs += dentry->rec_len;
	}

	/* no space in this block */
	if (offs >= ext2->block_size) {
		/* block alloc */
		d->inode->size += ext2->block_size;
		d->inode->blocks += ext2->block_size / 512;
		offs = 0;
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

	ext2_write(&d->oid, d->inode->size - ext2->block_size, data, ext2->block_size);

	free(data);
	return EOK;
}
