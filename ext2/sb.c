/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * SuperBlock
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

#include "sb.h"


int ext2_sb_sync(ext2_t *fs)
{
	off_t offset = SB_OFFSET;
	void *data = fs->sb;
	size_t size = sizeof(ext2_sb_t);
	if (fs->strg != NULL) {
		if (fs->strg->dev->blk->ops->write(fs->strg, fs->strg->start + offset, data, size) != size) {
			return -EIO;
		}
	}
	else if (fs->legacy.write != NULL) {
		if (fs->legacy.write(fs->legacy.devId, offset, data, size) != size) {
			return -EIO;
		}
	}
	else {
		return -ENOSYS;
	}

	return EOK;
}


void ext2_sb_destroy(ext2_t *fs)
{
	ext2_sb_sync(fs);
	free(fs->sb);
}


int ext2_sb_init(ext2_t *fs)
{
	off_t offset = SB_OFFSET;
	size_t size = sizeof(ext2_sb_t);

	if ((fs->sb = (ext2_sb_t *)malloc(sizeof(ext2_sb_t))) == NULL)
		return -ENOMEM;

	if (fs->strg != NULL) {
		if (fs->strg->dev->blk->ops->read(fs->strg, fs->strg->start + offset, fs->sb, size) != size) {
			free(fs->sb);
			return -EIO;
		}
	}
	else if (fs->legacy.read != NULL) {
		if (fs->legacy.read(fs->legacy.devId, offset, (char *)fs->sb, size) != size) {
			free(fs->sb);
			return -EIO;
		}
	}
	else {
		return -ENOSYS;
	}

	if (fs->sb->magic != MAGIC_EXT2) {
		free(fs->sb);
		return -ENOENT;
	}

	//TODO: features check

	if (!fs->sb->inodesz)
		fs->sb->inodesz = 128;

	fs->blocksz = 1024 << fs->sb->logBlocksz;

	return EOK;
}
