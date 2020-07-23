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
#include <stdlib.h>
#include <string.h>

#include "block.h"
#include "gdt.h"


int ext2_gdt_syncone(ext2_t *fs, uint32_t group)
{
	uint32_t blocks = (sizeof(ext2_gd_t) - 1) / fs->blocksz + 1;
	uint32_t bno = fs->sb->fstBlock + group * sizeof(ext2_gd_t) / fs->blocksz + 1;
	void *buff;
	int err;

	if ((buff = malloc(blocks * fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_block_read(fs, bno, buff, blocks)) < 0) {
		free(buff);
		return err;
	}

	memcpy(buff + group * sizeof(ext2_gd_t) % fs->blocksz, fs->gdt + group, sizeof(ext2_gd_t));

	if ((err = ext2_block_write(fs, bno, buff, blocks)) < 0) {
		free(buff);
		return err;
	}

	free(buff);

	return EOK;
}


int ext2_gdt_sync(ext2_t *fs)
{
	uint32_t gdtsz = fs->groups * sizeof(ext2_gd_t);
	uint32_t blocks = gdtsz / fs->blocksz;
	uint32_t bno = fs->sb->fstBlock + 1;
	void *buff;
	int err;

	if ((err = ext2_block_write(fs, bno, fs->gdt, blocks)) < 0)
		return err;

	if (gdtsz % fs->blocksz) {
		if ((buff = malloc(fs->blocksz)) == NULL)
			return -ENOMEM;

		if ((err = ext2_block_read(fs, bno + blocks, buff, 1)) < 0) {
			free(buff);
			return err;
		}

		memcpy(buff, (char *)fs->gdt + blocks * fs->blocksz, gdtsz % fs->blocksz);

		if ((err = ext2_block_write(fs, bno + blocks, buff, 1)) < 0) {
			free(buff);
			return err;
		}

		free(buff);
	}

	return EOK;
}


void ext2_gdt_destroy(ext2_t *fs)
{
	ext2_gdt_sync(fs);
	free(fs->gdt);
}


int ext2_gdt_init(ext2_t *fs)
{
	uint32_t groups = (fs->sb->inodes - 1) / fs->sb->groupInodes + 1;
	uint32_t gdtsz = groups * sizeof(ext2_gd_t);
	uint32_t blocks = gdtsz / fs->blocksz;
	uint32_t bno = fs->sb->fstBlock + 1;
	void *buff;
	int err;

	if ((fs->gdt = (ext2_gd_t *)malloc(gdtsz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_block_read(fs, bno, fs->gdt, blocks)) < 0) {
		free(fs->gdt);
		return err;
	}

	if (gdtsz % fs->blocksz) {
		if ((buff = malloc(fs->blocksz)) == NULL) {
			free(fs->gdt);
			return -ENOMEM;
		}

		if ((err = ext2_block_read(fs, bno + blocks, buff, 1)) < 0) {
			free(fs->gdt);
			free(buff);
			return err;
		}

		memcpy((char *)fs->gdt + blocks * fs->blocksz, buff, gdtsz % fs->blocksz);
		free(buff);
	}

	fs->groups = groups;

	return EOK;
}
