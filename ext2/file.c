/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * File operations
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "block.h"
#include "file.h"


ssize_t _ext2_file_read(ext2_t *fs, ext2_obj_t *obj, offs_t offs, char *buff, size_t len)
{
	uint32_t block = offs / fs->blocksz;
	size_t l = 0;
	void *data;
	int err;

	if ((offs < 0) || (offs >= obj->inode->size))
		return 0;

	if (len > obj->inode->size - offs)
		len = obj->inode->size - offs;

	if (!len)
		return 0;

	if (offs % fs->blocksz || len < fs->blocksz) {
		if ((data = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_block_init(fs, obj, block, data)) < 0) {
			free(data);
			return err;
		}

		if ((l = fs->blocksz - offs % fs->blocksz) > len)
			l = len;

		memcpy(buff, data + offs % fs->blocksz, l);
		free(data);
		block++;
	}

	for (; block < (offs + len) / fs->blocksz; block++, l += fs->blocksz) {
		if ((err = ext2_block_init(fs, obj, block, buff + l)) < 0)
			return err;
	}

	if (len > l) {
		if ((data = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_block_init(fs, obj, block, data)) < 0) {
			free(data);
			return err;
		}

		memcpy(buff + l, data, len - l);
		free(data);
	}

	obj->inode->atime = time(NULL);

	return len;
}


ssize_t _ext2_file_write(ext2_t *fs, ext2_obj_t *obj, offs_t offs, const char *buff, size_t len)
{
	uint32_t block = offs / fs->blocksz;
	size_t l = 0;
	void *data;
	int err;

	if (!len)
		return 0;

	if (offs % fs->blocksz || len < fs->blocksz) {
		if ((data = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_block_init(fs, obj, block, data)) < 0) {
			free(data);
			return err;
		}

		if ((l = fs->blocksz - offs % fs->blocksz) > len)
			l = len;

		memcpy(data + offs % fs->blocksz, buff, l);

		if ((err = ext2_block_syncone(fs, obj, block, data)) < 0) {
			free(data);
			return err;
		}

		free(data);
		block++;
	}

	if (block < (offs + len) / fs->blocksz) {
		if ((err = ext2_block_sync(fs, obj, block, buff + l, (offs + len) / fs->blocksz - block)) < 0)
			return err;

		l += fs->blocksz * ((offs + len) / fs->blocksz - block);
		block = (offs + len) / fs->blocksz;
	}

	if (len > l) {
		if ((data = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_block_init(fs, obj, block, data)) < 0) {
			free(data);
			return err;
		}

		memcpy(data, buff + l, len - l);

		if ((err = ext2_block_syncone(fs, obj, block, data)) < 0) {
			free(data);
			return err;
		}

		free(data);
	}

	if (offs + len > obj->inode->size)
		obj->inode->size = offs + len;

	obj->inode->mtime = obj->inode->atime = time(NULL);
	obj->flags |= OFLAG_DIRTY;

	if ((err = _ext2_obj_sync(fs, obj)) < 0)
		return err;

	if ((err = ext2_sb_sync(fs)) < 0)
		return err;

	return len;
}


int _ext2_file_truncate(ext2_t *fs, ext2_obj_t *obj, size_t size)
{
	uint32_t *bno, block, lbno = 0, n = 0;
	uint32_t start = (size + fs->blocksz - 1) / fs->blocksz;
	uint32_t end = (obj->inode->size + fs->blocksz - 1) / fs->blocksz;
	int err;

	/* FIXME: truncation for files with unallocated blocks might fail */

	if (obj->inode->size > size) {
		for (block = start; block < end; block++) {
			if ((err = ext2_block_get(fs, obj, block, &bno)) < 0)
				return err;

			/* count consecutive blocks to destroy them with one call */
			if (!lbno || (*bno == lbno + 1)) {
				n++;
			}
			else {
				if ((err = ext2_block_destroy(fs, lbno + 1 - n, n)) < 0)
					return err;

				n = 1;
			}

			lbno = *bno;
		}

		if ((n > 0) && (err = ext2_block_destroy(fs, lbno + 1 - n, n)) < 0)
			return err;

		if ((err = ext2_iblock_destroy(fs, obj, start, end - start)) < 0)
			return err;
	}

	obj->inode->size = size;
	/* FIXME: blocks counting is broken, move it to iblock_destroy */
	obj->inode->blocks -= (end - start) * fs->blocksz / fs->sectorsz;
	obj->inode->mtime = obj->inode->atime = time(NULL);
	obj->flags |= OFLAG_DIRTY;

	return EOK;
}
