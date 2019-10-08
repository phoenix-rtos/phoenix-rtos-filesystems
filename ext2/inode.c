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

#include "ext2.h"
#include "inode.h"
#include "block.h"
#include "sb.h"


ext2_inode_t *inode_get(uint32_t ino)
{
	void *inode_block;
	ext2_inode_t *inode;
	uint32_t group;
	uint32_t block;
	uint32_t inodes_in_block;

	if (ino < ROOTNODE_NO)
		return NULL;
	if (ino > ext2->inodes_count)
		return NULL;

	inode_block = malloc(ext2->block_size);
	group = (ino - 1) / ext2->inodes_in_group;

	inodes_in_block = (ext2->block_size / ext2->inode_size);
	block = ((ino - 1) % ext2->inodes_in_group) / inodes_in_block;

	read_block(ext2->gdt[group].ext2_inode_table + block, inode_block);

	inode = malloc(ext2->inode_size);

	memcpy(inode, inode_block + (((ino - 1) % inodes_in_block) * ext2->inode_size), ext2->inode_size);
	free(inode_block);
	return inode;
}

int inode_put(ext2_inode_t *inode)
{
	free(inode);
	return EOK;
}

int inode_set(uint32_t ino, ext2_inode_t *inode)
{
	void *inode_block;
	uint32_t group;
	uint32_t block;
	uint32_t inodes_in_block;

	if (ino < ROOTNODE_NO)
		return -EINVAL;
	if (ino > ext2->inodes_count)
		return -EINVAL;

	inode_block = malloc(ext2->block_size);
	group = (ino - 1) / ext2->inodes_in_group;

	inodes_in_block = (ext2->block_size / ext2->inode_size);
	block = ((ino - 1) % ext2->inodes_in_group) / inodes_in_block;

	read_block(ext2->gdt[group].ext2_inode_table + block, inode_block);

	memcpy(inode_block + (((ino - 1) % inodes_in_block) * ext2->inode_size), inode, ext2->inode_size);
	write_block(ext2->gdt[group].ext2_inode_table + block, inode_block);

	free(inode_block);
	return EOK;
}

static int find_group_dir(uint32_t pino)
{
	int i;
	int pgroup = (pino - 1) / ext2->inodes_in_group;
	int group = -1;
	int avefreei = ext2->sb->free_inodes_count / ext2->gdt_size;
	int avefreeb = ext2->sb->free_blocks_count / ext2->gdt_size;

	if(pino == ROOTNODE_NO)
		pgroup = rand() % ext2->gdt_size;
	for (i = 0; i < ext2->gdt_size; i++) {
		group = (pgroup + i) % ext2->gdt_size;
		if (ext2->gdt[group].free_inodes_count < avefreei)
			continue;
		if (ext2->gdt[group].free_blocks_count < avefreeb)
			continue;
	}
	if(group >= 0)
		return group;

	do {
		for(i = 0; i < ext2->gdt_size; i++) {
			group = (pgroup + i) % ext2->gdt_size;
			if (ext2->gdt[group].free_inodes_count >= avefreei)
				return group;
		}

		if (avefreei)
			avefreei = 0;
		else
			break;
	} while (!group);

	return group;
}


static int find_group_file(uint32_t pino)
{
	int i;
	int ngroups = ext2->gdt_size;
	int pgroup = (pino - 1) / ext2->inodes_in_group;
	int group;

	if (ext2->gdt[pgroup].free_inodes_count && ext2->gdt[pgroup].free_blocks_count)
		return pgroup;

	group = (pgroup + pino) % ngroups;

	for (i = 0; i < ngroups; i <<= 1) {
		group += i;
		group = group % ngroups;
		if (ext2->gdt[group].free_inodes_count && ext2->gdt[group].free_blocks_count)
			return group;
	}

	group = pgroup;

	for (i = 0; i < ngroups; i++) {
		group++;
		group = group % ngroups;
		if (ext2->gdt[group].free_inodes_count)
			return group;
	}

	return -1;
}

uint32_t inode_create(ext2_inode_t *inode, uint32_t mode, uint32_t pino)
{
	uint32_t ino;
	int group;
	void *inode_bmp;

	if (mode & EXT2_S_IFDIR)
		group = find_group_dir(pino);
	else
		group = find_group_file(pino);

	if (group == -1)
		return 0;

	inode_bmp = malloc(ext2->block_size);

	read_block(ext2->gdt[group].inode_bitmap, inode_bmp);

	ino = find_zero_bit(inode_bmp, ext2->inodes_in_group, 0);

	if (ino > ext2->inodes_in_group || ino <= 0) {
		free(inode_bmp);
		return 0;
	}

	toggle_bit(inode_bmp, ino);

	ext2->gdt[group].free_inodes_count--;
	if (mode & EXT2_S_IFDIR)
		ext2->gdt[group].used_dirs_count++;
	ext2->sb->free_inodes_count--;

	memset(inode, 0, ext2->inode_size);
	inode->mode = mode | EXT2_S_IRUSR | EXT2_S_IWUSR;

	write_block(ext2->gdt[group].inode_bitmap, inode_bmp);
	gdt_sync(group);
	ext2_write_sb();
	free(inode_bmp);

	return group * ext2->inodes_in_group + ino;
}


int inode_free(uint32_t ino, ext2_inode_t *inode)
{

	uint32_t group = (ino - 1) / ext2->inodes_in_group;
	void *inode_bmp = malloc(ext2->block_size);

	if (inode->mode & EXT2_S_IFDIR)
		ext2->gdt[group].used_dirs_count--;

	ext2->gdt[group].free_inodes_count++;
	ext2->sb->free_inodes_count++;

	ino = (ino - 1) % ext2->inodes_in_group;

	read_block(ext2->gdt[group].inode_bitmap, inode_bmp);
	toggle_bit(inode_bmp, ino + 1);
	write_block(ext2->gdt[group].inode_bitmap, inode_bmp);
	gdt_sync(group);
	ext2_write_sb();

	free(inode);

	return EOK;
}
