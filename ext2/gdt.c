/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Group Descriptor Table
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
#include <string.h>

#include "block.h"
#include "gdt.h"


int ext2_init_gdt(ext2_t *fs)
{
	ext2_gd_t *gdt;
	void *buff;
	int err;
	uint32_t groups = (fs->sb->blocks - 1) / fs->sb->group_blocks + 1;
	uint32_t gdtsz = groups * sizeof(ext2_gd_t);
	uint32_t start = fs->sb->fst_block + 1;
	uint32_t blocks = gdtsz / fs->blocksz;

	if ((gdt = (ext2_gd_t *)malloc(gdtsz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_read_blocks(fs, start, blocks, gdt)) < 0)
		return err;

	if (gdtsz % fs->blocksz) {
		if ((buff = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_read_blocks(fs, start + blocks, 1, buff)) < 0)
			return err;

		memcpy((void *)((uintptr_t)(void *)gdt + blocks * fs->blocksz), buff, gdtsz % fs->blocksz);
		free(buff);
	}

	fs->gdt = gdt;
	fs->groups = groups;

	return EOK;
}


int ext2_sync_gd(ext2_t *fs, uint32_t group)
{
	void *buff;
	int err;
	uint32_t start = fs->sb->fst_block + group * sizeof(ext2_gd_t) / fs->blocksz + 1;
	uint32_t blocks = (sizeof(ext2_gd_t) - 1) / fs->blocksz + 1;

	if ((buff = malloc(blocks * fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_read_blocks(fs, start, blocks, buff)) < 0)
		return err;

	memcpy(buff + group * sizeof(ext2_gd_t) % fs->blocksz, fs->gdt + group, sizeof(ext2_gd_t));

	if ((err = ext2_write_blocks(fs, start, blocks, buff)) < 0)
		return err;

	return EOK;
}


int ext2_sync_gdt(ext2_t *fs)
{
	void *buff;
	int err;
	uint32_t gdtsz = fs->groups * sizeof(ext2_gd_t);
	uint32_t start = fs->sb->fst_block + 1;
	uint32_t blocks = gdtsz / fs->blocksz;

	if ((err = ext2_write_blocks(fs, start, blocks, fs->gdt)) < 0)
		return err;

	if (gdtsz % fs->blocksz) {
		if ((buff = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_read_blocks(fs, start + blocks, 1, buff)) < 0)
			return err;

		memcpy(buff, (void *)((uintptr_t)fs->gdt + blocks * fs->blocksz), gdtsz % fs->blocksz);

		if ((err = ext2_write_blocks(fs, start + blocks, 1, buff)) < 0)
			return err;

		free(buff);
	}

	return EOK;
}
