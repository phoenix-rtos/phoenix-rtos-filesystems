/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * inode.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <phoenix/stat.h>

#include "ext2.h"
#include "inode.h"
#include "block.h"
#include "sb.h"


ext2_inode_t *inode_get(ext2_fs_info_t *f, uint32_t ino)
{
	void *inode_block;
	ext2_inode_t *inode;
	uint32_t group;
	uint32_t block;
	uint32_t inodes_in_block;

	if (ino < f->root->id)
		return NULL;
	if (ino > f->inodes_count)
		return NULL;

	inode_block = malloc(f->block_size);
	group = (ino - 1) / f->inodes_in_group;

	inodes_in_block = (f->block_size / f->inode_size);
	block = ((ino - 1) % f->inodes_in_group) / inodes_in_block;

	read_block(f, f->gdt[group].ext2_inode_table + block, inode_block);

	inode = malloc(f->inode_size);

	memcpy(inode, inode_block + (((ino - 1) % inodes_in_block) * f->inode_size), f->inode_size);
	free(inode_block);
	return inode;
}

int inode_put(ext2_inode_t *inode)
{
	free(inode);
	return EOK;
}

int inode_set(ext2_fs_info_t *f, uint32_t ino, ext2_inode_t *inode)
{
	void *inode_block;
	uint32_t group;
	uint32_t block;
	uint32_t inodes_in_block;

	if (ino < f->root->id)
		return -EINVAL;
	if (ino > f->inodes_count)
		return -EINVAL;

	inode_block = malloc(f->block_size);
	group = (ino - 1) / f->inodes_in_group;

	inodes_in_block = (f->block_size / f->inode_size);
	block = ((ino - 1) % f->inodes_in_group) / inodes_in_block;

	read_block(f, f->gdt[group].ext2_inode_table + block, inode_block);

	memcpy(inode_block + (((ino - 1) % inodes_in_block) * f->inode_size), inode, f->inode_size);
	write_block(f, f->gdt[group].ext2_inode_table + block, inode_block);

	free(inode_block);
	return EOK;
}

static int find_group_dir(ext2_fs_info_t *f, uint32_t pino)
{
	int i;
	int pgroup = (pino - 1) / f->inodes_in_group;
	int group = -1;
	int avefreei = f->sb->free_inodes_count / f->gdt_size;
	int avefreeb = f->sb->free_blocks_count / f->gdt_size;

	if(pino == f->root->id)
		pgroup = rand() % f->gdt_size;
	for (i = 0; i < f->gdt_size; i++) {
		group = (pgroup + i) % f->gdt_size;
		if (f->gdt[group].free_inodes_count < avefreei)
			continue;
		if (f->gdt[group].free_blocks_count < avefreeb)
			continue;
	}
	if(group >= 0)
		return group;

	do {
		for(i = 0; i < f->gdt_size; i++) {
			group = (pgroup + i) % f->gdt_size;
			if (f->gdt[group].free_inodes_count >= avefreei)
				return group;
		}

		if (avefreei)
			avefreei = 0;
		else
			break;
	} while (!group);

	return group;
}


static int find_group_file(ext2_fs_info_t *f, uint32_t pino)
{
	int i;
	int ngroups = f->gdt_size;
	int pgroup = (pino - 1) / f->inodes_in_group;
	int group;

	if (f->gdt[pgroup].free_inodes_count && f->gdt[pgroup].free_blocks_count)
		return pgroup;

	group = (pgroup + pino) % ngroups;

	for (i = 1; i < ngroups; i <<= 1) {
		group += i;
		group = group % ngroups;
		if (f->gdt[group].free_inodes_count && f->gdt[group].free_blocks_count)
			return group;
	}

	group = pgroup;

	for (i = 0; i < ngroups; i++) {
		group++;
		group = group % ngroups;
		if (f->gdt[group].free_inodes_count)
			return group;
	}

	return -1;
}

uint32_t inode_create(ext2_fs_info_t *f, ext2_inode_t *inode, uint32_t mode, uint32_t pino)
{
	uint32_t ino;
	int group;
	void *inode_bmp;

	if (mode & S_IFDIR)
		group = find_group_dir(f, pino);
	else
		group = find_group_file(f, pino);

	if (group == -1)
		return 0;

	inode_bmp = malloc(f->block_size);

	read_block(f, f->gdt[group].inode_bitmap, inode_bmp);

	ino = find_zero_bit(inode_bmp, f->inodes_in_group, 0);

	if (ino > f->inodes_in_group || ino <= 0) {
		free(inode_bmp);
		return 0;
	}

	toggle_bit(inode_bmp, ino);

	f->gdt[group].free_inodes_count--;
	if (mode & S_IFDIR)
		f->gdt[group].used_dirs_count++;
	f->sb->free_inodes_count--;

	memset(inode, 0, f->inode_size);
	inode->mode = mode;

	write_block(f, f->gdt[group].inode_bitmap, inode_bmp);
	gdt_sync(f, group);
	ext2_write_sb(f);
	free(inode_bmp);

	return group * f->inodes_in_group + ino;
}


int inode_free(ext2_fs_info_t *f, uint32_t ino, ext2_inode_t *inode)
{

	uint32_t group = (ino - 1) / f->inodes_in_group;
	void *inode_bmp = malloc(f->block_size);

	if (inode->mode & S_IFDIR)
		f->gdt[group].used_dirs_count--;

	f->gdt[group].free_inodes_count++;
	f->sb->free_inodes_count++;

	ino = (ino - 1) % f->inodes_in_group;

	read_block(f, f->gdt[group].inode_bitmap, inode_bmp);
	toggle_bit(inode_bmp, ino + 1);
	write_block(f, f->gdt[group].inode_bitmap, inode_bmp);
	gdt_sync(f, group);
	ext2_write_sb(f);

	free(inode);

	return EOK;
}
