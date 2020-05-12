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
#include <stdint.h>
#include <stdlib.h>

#include "sb.h"


int ext2_init_sb(ext2_t *fs)
{
	ext2_sb_t *sb;
	ssize_t ret;

	if ((sb = (ext2_sb_t *)malloc(sizeof(ext2_sb_t))) == NULL)
		return -ENOMEM;

	if ((ret = fs->read(fs->dev, SB_OFFSET, (char *)sb, sizeof(ext2_sb_t))) != sizeof(ext2_sb_t))
		return (int)ret;

	if (sb->magic != MAGIC_EXT2)
		return -ENOENT;

	//TODO: features check

	fs->sb = sb;
	fs->blocksz = 1024 << sb->log_blocksz;

	return EOK;
}


int ext2_sync_sb(ext2_t *fs)
{
	ssize_t ret;

	if ((ret = fs->write(fs->dev, SB_OFFSET, (char *)fs->sb, sizeof(ext2_sb_t))) != sizeof(ext2_sb_t))
		return (int)ret;

	return EOK;
}
