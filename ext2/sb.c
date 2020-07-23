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
	if (fs->write(fs->oid.id, SB_OFFSET, (char *)fs->sb, sizeof(ext2_sb_t)) != sizeof(ext2_sb_t))
		return -EIO;

	return EOK;
}


void ext2_sb_destroy(ext2_t *fs)
{
	ext2_sb_sync(fs);
	free(fs->sb);
}


int ext2_sb_init(ext2_t *fs)
{
	if ((fs->sb = (ext2_sb_t *)malloc(sizeof(ext2_sb_t))) == NULL)
		return -ENOMEM;

	if (fs->read(fs->oid.id, SB_OFFSET, (char *)fs->sb, sizeof(ext2_sb_t)) != sizeof(ext2_sb_t)) {
		free(fs->sb);
		return -EIO;
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
