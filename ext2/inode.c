/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Inode
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

#include "ext2.h"
#include "block.h"
#include "inode.h"
#include "sb.h"
#include "gdt.h"


static uint32_t ext2_file_group(ext2_t *fs, uint32_t pino)
{
	uint32_t i;
	uint32_t pgroup = (pino - 1) / fs->sb->group_inodes;
	uint32_t group = (pgroup + pino) % fs->groups;

	if (fs->gdt[pgroup].free_inodes && fs->gdt[pgroup].free_blocks)
		return pgroup;

	for (i = 1; i < fs->groups; i <<= 1) {
		group = (group + i) % fs->groups;

		if (fs->gdt[group].free_inodes && fs->gdt[group].free_blocks)
			return group;
	}

	for (i = 0, group = pgroup; i < fs->groups; i++) {
		group = (group + 1) % fs->groups;

		if (fs->gdt[group].free_inodes)
			return group;
	}

	return -1;
}


static uint32_t ext2_dir_group(ext2_t *fs, uint32_t pino)
{
	uint32_t i, pgroup, group;
	uint32_t ifree = fs->sb->free_inodes / fs->groups;
	uint32_t bfree = fs->sb->free_blocks / fs->groups;

	if (fs->root && pino == (uint32_t)fs->root->ino)
		pgroup = rand() % fs->groups;
	else
		pgroup = (pino - 1) / fs->sb->group_inodes;

	for (i = 0; i < fs->groups; i++) {
		group = (pgroup + i) % fs->groups;

		if (fs->gdt[group].free_inodes < ifree)
			continue;

		if (fs->gdt[group].free_blocks < bfree)
			continue;

		return group;
	}

	for (i = 0; i < fs->groups; i++) {
		group = (pgroup + i) % fs->groups;

		if (fs->gdt[group].free_inodes < ifree)
			continue;
		
		return group;
	}

	return -1;
}


uint32_t ext2_create_inode(ext2_t *fs, uint32_t pino, uint16_t mode)
{
	void *inode_bmp;
	uint32_t group;
	uint32_t ino;

	if (mode & MODE_DIR)
		group = ext2_dir_group(fs, pino);
	else
		group = ext2_file_group(fs, pino);

	if (group == -1)
		return 0;

	if ((inode_bmp = malloc(fs->blocksz)) == NULL)
		return 0;

	if (ext2_read_blocks(fs, fs->gdt[group].inode_bmp, 1, inode_bmp) < 0) {
		free(inode_bmp);
		return 0;
	}

	if (!(ino = ext2_find_zero_bit(inode_bmp, fs->sb->group_inodes, 0))) {
		free(inode_bmp);
		return 0;
	}

	ext2_toggle_bit(inode_bmp, ino);

	if (ext2_write_blocks(fs, fs->gdt[group].inode_bmp, 1, inode_bmp) < 0) {
		free(inode_bmp);
		return 0;
	}

	free(inode_bmp);

	if (mode & MODE_DIR)
		fs->gdt[group].dirs++;

	fs->gdt[group].free_inodes--;

	if (ext2_sync_gd(fs, group) < 0)
		return 0;

	fs->sb->free_inodes--;

	if (ext2_sync_sb(fs) < 0)
		return 0;

	return group * fs->sb->group_inodes + ino;
}


int ext2_destroy_inode(ext2_t *fs, uint32_t ino, uint16_t mode)
{
	uint32_t group = (ino - 1) / fs->sb->group_inodes;
	void *inode_bmp;
	int err;
	
	if ((inode_bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_read_blocks(fs, fs->gdt[group].inode_bmp, 1, inode_bmp)) < 0) {
		free(inode_bmp);
		return err;
	}

	ext2_toggle_bit(inode_bmp, (ino - 1) % fs->sb->group_inodes + 1);

	if ((err = ext2_write_blocks(fs, fs->gdt[group].inode_bmp, 1, inode_bmp)) < 0) {
		free(inode_bmp);
		return err;
	}

	free(inode_bmp);

	if (mode & MODE_DIR)
		fs->gdt[group].dirs--;

	fs->gdt[group].free_inodes++;

	if ((err = ext2_sync_gd(fs, group)) < 0)
		return err;

	fs->sb->free_inodes++;

	if ((err = ext2_sync_sb(fs)) < 0)
		return err;

	return EOK;
}


ext2_inode_t *ext2_init_inode(ext2_t *fs, uint32_t ino)
{
	ext2_inode_t *inode;
	void *iblock;
	uint32_t group = (ino - 1) / fs->sb->group_inodes;
	uint32_t inodes = fs->blocksz / fs->sb->inodesz;
	uint32_t start = fs->gdt[group].inode_tbl + (ino - 1) % fs->sb->group_inodes / inodes;

	if ((fs->root && ino < (uint32_t)fs->root->ino) || ino > fs->sb->inodes)
		return NULL;

	if ((iblock = malloc(fs->blocksz)) == NULL)
		return NULL;

	if (ext2_read_blocks(fs, start, 1, iblock) < 0) {
		free(iblock);
		return NULL;
	}

	if ((inode = (ext2_inode_t *)malloc(fs->sb->inodesz)) == NULL) {
		free(iblock);
		return NULL;
	}

	memcpy(inode, iblock + (ino - 1) % inodes * fs->sb->inodesz, fs->sb->inodesz);
	free(iblock);

	return inode;
}


int ext2_sync_inode(ext2_t *fs, uint32_t ino, ext2_inode_t *inode)
{
	void *iblock;
	int err;
	uint32_t group = (ino - 1) / fs->sb->group_inodes;
	uint32_t inodes = fs->blocksz / fs->sb->inodesz;
	uint32_t start = fs->gdt[group].inode_tbl + (ino - 1) % fs->sb->group_inodes / inodes;

	if ((fs->root && ino < (uint32_t)fs->root->ino) || ino > fs->sb->inodes)
		return -EINVAL;

	if ((iblock = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_read_blocks(fs, start, 1, iblock)) < 0) {
		free(iblock);
		return err;
	}

	memcpy(iblock + (ino - 1) % inodes * fs->sb->inodesz, inode, fs->sb->inodesz);

	if ((err = ext2_write_blocks(fs, start, 1, iblock)) < 0) {
		free(iblock);
		return err;
	}

	free(iblock);

	return EOK;
}
