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
#include <sys/minmax.h>
#include <sys/stat.h>

#include "block.h"
#include "file.h"


ssize_t _ext2_file_read(ext2_t *fs, ext2_obj_t *obj, off_t offs, char *buff, size_t len)
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

	if (S_ISLNK(obj->inode->mode) && (obj->inode->size <= MAX_SYMLINK_LEN_IN_INODE)) {
		memcpy((void *)buff, (const void *)(((const char *)obj->inode->block) + offs), len);
		return len;
	}

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


ssize_t _ext2_file_write(ext2_t *fs, ext2_obj_t *obj, off_t offs, const char *buff, size_t len)
{
	if (len == 0) {
		return 0;
	}

	/* Link can only be written to during creation. */
	if (S_ISLNK(obj->inode->mode) && ((offs != 0) || (obj->inode->size != 0))) {
		return -EINVAL;
	}

	int err;
	if (S_ISLNK(obj->inode->mode) && (len <= MAX_SYMLINK_LEN_IN_INODE)) {
		memcpy((void *)(obj->inode->block), (const void *)buff, len);
	}
	else {
		uint32_t block = offs / fs->blocksz;
		uint32_t offsInBlock = offs % fs->blocksz;
		size_t written = 0;

		if ((offsInBlock != 0) || (len < fs->blocksz)) {
			void *data = malloc(fs->blocksz);
			if (data == NULL) {
				return -ENOMEM;
			}

			err = ext2_block_init(fs, obj, block, data);
			if (err < 0) {
				free(data);
				return err;
			}

			written = min(fs->blocksz - offsInBlock, len);
			memcpy(data + offsInBlock, buff, written);

			err = ext2_block_syncone(fs, obj, block, data);
			if (err < 0) {
				free(data);
				return err;
			}

			free(data);
			block++;
		}

		const uint32_t fullBlocks = (len - written) / fs->blocksz;
		if (fullBlocks > 0) {
			err = ext2_block_sync(fs, obj, block, buff + written, fullBlocks);
			if (err < 0) {
				return err;
			}

			written += fs->blocksz * fullBlocks;
			block += fullBlocks;
		}

		if (len > written) {
			void *data = malloc(fs->blocksz);
			if (data == NULL) {
				return -ENOMEM;
			}

			err = ext2_block_init(fs, obj, block, data);
			if (err < 0) {
				free(data);
				return err;
			}

			memcpy(data, buff + written, len - written);

			err = ext2_block_syncone(fs, obj, block, data);
			if (err < 0) {
				free(data);
				return err;
			}

			free(data);
		}
	}

	if ((offs + len) > obj->inode->size) {
		obj->inode->size = offs + len;
	}

	obj->inode->mtime = obj->inode->ctime = time(NULL);
	obj->flags |= OFLAG_DIRTY;

	err = _ext2_obj_sync(fs, obj);
	if (err < 0) {
		return err;
	}

	err = ext2_sb_sync(fs);
	if (err < 0) {
		return err;
	}

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
	obj->inode->mtime = obj->inode->ctime = time(NULL);
	obj->flags |= OFLAG_DIRTY;

	return EOK;
}
