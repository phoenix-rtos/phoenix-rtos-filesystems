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

#include <sys/stat.h>

#include "block.h"
#include "inode.h"


int ext2_inode_sync(ext2_t *fs, uint32_t ino, ext2_inode_t *inode)
{
	uint32_t group = (ino - 1) / fs->sb->groupInodes;
	uint32_t inodes = fs->blocksz / fs->sb->inodesz;
	uint32_t bno = fs->gdt[group].inodeTbl + ((ino - 1) % fs->sb->groupInodes) / inodes;
	char *buff;
	int err;

	if (((fs->root != NULL) && (ino < (uint32_t)fs->root->id)) || (ino > fs->sb->inodes))
		return -EINVAL;

	if ((buff = (char *)malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_block_read(fs, bno, buff, 1)) < 0) {
		free(buff);
		return err;
	}

	memcpy(buff + ((ino - 1) % inodes) * fs->sb->inodesz, inode, fs->sb->inodesz);

	if ((err = ext2_block_write(fs, bno, buff, 1)) < 0) {
		free(buff);
		return err;
	}

	free(buff);

	return EOK;
}


ext2_inode_t *ext2_inode_init(ext2_t *fs, uint32_t ino)
{
	uint32_t group = (ino - 1) / fs->sb->groupInodes;
	uint32_t inodes = fs->blocksz / fs->sb->inodesz;
	uint32_t bno = fs->gdt[group].inodeTbl + ((ino - 1) % fs->sb->groupInodes) / inodes;
	ext2_inode_t *inode;
	char *buff;

	if (((fs->root != NULL) && (ino < (uint32_t)fs->root->id)) || (ino > fs->sb->inodes))
		return NULL;

	if ((buff = (char *)malloc(fs->blocksz)) == NULL)
		return NULL;

	if (ext2_block_read(fs, bno, buff, 1) < 0) {
		free(buff);
		return NULL;
	}

	if ((inode = (ext2_inode_t *)malloc(fs->sb->inodesz)) == NULL) {
		free(buff);
		return NULL;
	}

	memcpy(inode, buff + ((ino - 1) % inodes) * fs->sb->inodesz, fs->sb->inodesz);
	free(buff);

	return inode;
}


int ext2_inode_destroy(ext2_t *fs, uint32_t ino, uint16_t mode)
{
	uint32_t group = (ino - 1) / fs->sb->groupInodes;
	void *bmp;
	int err;

	if (((fs->root != NULL) && (ino < (uint32_t)fs->root->id)) || (ino > fs->sb->inodes))
		return -EINVAL;

	if ((bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_block_read(fs, fs->gdt[group].inodeBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	ext2_togglebit(bmp, (ino - 1) % fs->sb->groupInodes + 1);

	if ((err = ext2_block_write(fs, fs->gdt[group].inodeBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	if (S_ISDIR(mode))
		fs->gdt[group].dirs--;
	fs->gdt[group].freeInodes++;

	if ((err = ext2_gdt_syncone(fs, group)) < 0) {
		ext2_togglebit(bmp, (ino - 1) % fs->sb->groupInodes + 1);

		if (!ext2_block_write(fs, fs->gdt[group].inodeBmp, bmp, 1)) {
			if (S_ISDIR(mode))
				fs->gdt[group].dirs++;
			fs->gdt[group].freeInodes--;
		}
		else {
			fs->sb->freeInodes++;
			ext2_sb_sync(fs);
		}

		free(bmp);
		return err;
	}

	free(bmp);
	fs->sb->freeInodes++;

	return ext2_sb_sync(fs);
}


/* Calculates new inode file group */
static uint32_t ext2_inode_filegroup(ext2_t *fs, uint32_t pino)
{
	uint32_t pgroup = (pino - 1) / fs->sb->groupInodes;
	uint32_t i, group = (pgroup + pino) % fs->groups;

	if (fs->gdt[pgroup].freeInodes && fs->gdt[pgroup].freeBlocks)
		return pgroup;

	for (i = 1; i < fs->groups; i <<= 1) {
		group = (group + i) % fs->groups;

		if (fs->gdt[group].freeInodes && fs->gdt[group].freeBlocks)
			return group;
	}

	for (i = 0, group = pgroup; i < fs->groups; i++) {
		group = (group + 1) % fs->groups;

		if (fs->gdt[group].freeInodes)
			return group;
	}

	return fs->groups;
}


/* Calculates new inode directory group */
static uint32_t ext2_inode_dirgroup(ext2_t *fs, uint32_t pino)
{
	uint32_t ifree = fs->sb->freeInodes / fs->groups;
	uint32_t bfree = fs->sb->freeBlocks / fs->groups;
	uint32_t i, pgroup, group;

	if ((fs->root != NULL) && (pino == (uint32_t)fs->root->id))
		pgroup = rand() % fs->groups;
	else
		pgroup = (pino - 1) / fs->sb->groupInodes;

	for (i = 0; i < fs->groups; i++) {
		group = (pgroup + i) % fs->groups;

		if (fs->gdt[group].freeInodes < ifree)
			continue;

		if (fs->gdt[group].freeBlocks < bfree)
			continue;

		return group;
	}

	for (i = 0; i < fs->groups; i++) {
		group = (pgroup + i) % fs->groups;

		if (fs->gdt[group].freeInodes >= ifree)
			return group;
	}

	for (i = 0; i < fs->groups; i++) {
		group = (pgroup + i) % fs->groups;

		if (fs->gdt[group].freeInodes)
			return group;
	}

	return fs->groups;
}


uint32_t ext2_inode_create(ext2_t *fs, uint32_t pino, uint16_t mode)
{
	uint32_t group, ino;
	void *bmp;

	if (S_ISDIR(mode))
		group = ext2_inode_dirgroup(fs, pino);
	else
		group = ext2_inode_filegroup(fs, pino);

	if (group == fs->groups)
		return 0;

	if ((bmp = malloc(fs->blocksz)) == NULL)
		return 0;

	if (ext2_block_read(fs, fs->gdt[group].inodeBmp, bmp, 1) < 0) {
		free(bmp);
		return 0;
	}

	if (!(ino = ext2_findzerobit(bmp, fs->sb->groupInodes, 0))) {
		free(bmp);
		return 0;
	}

	ext2_togglebit(bmp, ino);

	if (ext2_block_write(fs, fs->gdt[group].inodeBmp, bmp, 1) < 0) {
		free(bmp);
		return 0;
	}

	if (S_ISDIR(mode))
		fs->gdt[group].dirs++;
	fs->gdt[group].freeInodes--;

	if (ext2_gdt_syncone(fs, group) < 0) {
		ext2_togglebit(bmp, ino);

		if (!ext2_block_write(fs, fs->gdt[group].inodeBmp, bmp, 1)) {
			if (S_ISDIR(mode))
				fs->gdt[group].dirs--;
			fs->gdt[group].freeInodes++;
		}
		else {
			fs->sb->freeInodes--;
			ext2_sb_sync(fs);
		}

		free(bmp);
		return 0;
	}

	free(bmp);
	fs->sb->freeInodes--;

	if (ext2_sb_sync(fs) < 0)
		return 0;

	return group * fs->sb->groupInodes + ino;
}
