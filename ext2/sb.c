/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * sb.c
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
#include <errno.h>
#include <string.h>

#include <atasrv.h>

#include "ext2.h"
#include "sb.h"
#include "block.h"
#include "object.h"


/* ext2 superblock size and offset. */
#define EXT2_SB_SZ 1024
#define EXT2_SB_OFF EXT2_SB_SZ

/* ext2 magic number */
#define EXT2_MAGIC 0xEF53

void gdt_sync(ext2_fs_info_t *f, int group)
{
	uint32_t bno = (group * sizeof(ext2_group_desc_t)) / f->block_size;

	write_block(f, 2 + bno, &(f->gdt[bno * (f->block_size / sizeof(ext2_group_desc_t))]));
}

int ext2_read_sb(id_t *devId, ext2_fs_info_t *f)
{
	int ret = 0, err;

	ret = atasrv_read(devId, EXT2_SB_OFF, (char *)f->sb, EXT2_SB_SZ, &err);

	if (ret != EXT2_SB_SZ)
		return -EFAULT;

	if (f->sb->magic != EXT2_MAGIC)
		return -EFAULT;

	return EOK;
}


int ext2_write_sb(ext2_fs_info_t *f)
{
	int ret = 0, err;

	ret = atasrv_write(&f->devId, EXT2_SB_OFF, (char *)f->sb, EXT2_SB_SZ, &err);

	if (ret != EXT2_SB_SZ) {
		printf("ext2: superblock write error %d\n", ret);
		return -EFAULT;
	}

	return EOK;
}


void ext2_init_fs(id_t *devId, ext2_fs_info_t *f)
{
	int size, err;
	void *buff;

	f->block_size = EXT2_SB_SZ << f->sb->log_block_size;
	f->blocks_count = f->sb->blocks_count;
	printf("ext2: Mounting %.2f MiB partition\n", (float)(f->block_size * f->blocks_count)/1024/1024);
	f->blocks_in_group = f->sb->blocks_in_group;
	f->inode_size = f->sb->inode_size;
	f->inodes_count = f->sb->inodes_count;
	f->inodes_in_group = f->sb->inodes_in_group;
	f->gdt_size = 1 + (f->sb->blocks_count - 1) / f->sb->blocks_in_group;
	f->gdt_size = 1 + (f->sb->inodes_count - 1) / f->sb->inodes_in_group;
	f->gdt =  calloc(1, f->gdt_size * sizeof(ext2_group_desc_t));
	f->devId = *devId;

	size = (f->gdt_size * sizeof(ext2_group_desc_t)) / f->block_size;

	if (size) {
		read_blocks(f, f->sb->first_data_block + 1, (f->gdt_size * sizeof(ext2_group_desc_t)) / f->block_size, f->gdt);
	}
	else {
		buff = malloc(f->block_size);
		size = (f->gdt_size * sizeof(ext2_group_desc_t));
		atasrv_read(&f->devId, (f->sb->first_data_block + 1) * f->block_size, buff, f->block_size, &err);
		memcpy(f->gdt, buff, size);
		free(buff);
	}
	//TODO: features check
}


int libext2_mount(id_t *devId, void **fsData)
{
	int ret = -EFAULT;
	id_t rootId = 2;
	ext2_fs_info_t *f = calloc(1, sizeof(ext2_fs_info_t));
	f->sb = calloc(1, EXT2_SB_SZ);

	*fsData = f;

	ret = ext2_read_sb(devId, f);

	if (ret != EOK) {
		printf("ext2: no ext2 partition found\n");
		return ret;
	}

	ext2_init_fs(devId, f);
	object_init(f);
	f->root = object_get(f, &rootId);
	return (int)rootId;
}
