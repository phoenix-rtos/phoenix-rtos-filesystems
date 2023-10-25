/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Block operations
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
#include "inode.h"


int ext2_block_read(ext2_t *fs, uint32_t bno, void *buff, uint32_t n)
{
	ssize_t size = n * fs->blocksz;
	if (fs->strg != NULL) {
		if (fs->strg->dev->blk->ops->read(fs->strg, fs->strg->start + bno * fs->blocksz, buff, size) != size) {
			return -EIO;
		}
	}
	else if (fs->legacy.read != NULL) {
		if (fs->legacy.read(fs->legacy.devId, bno * fs->blocksz, buff, size) != size) {
			return -EIO;
		}
	}
	else {
		return -ENOSYS;
	}

	return EOK;
}


int ext2_block_write(ext2_t *fs, uint32_t bno, const void *buff, uint32_t n)
{
	ssize_t size = n * fs->blocksz;
	if (fs->strg != NULL) {
		if (fs->strg->dev->blk->ops->write(fs->strg, fs->strg->start + bno * fs->blocksz, buff, size) != size) {
			return -EIO;
		}
	}
	else if (fs->legacy.write != NULL) {
		if (fs->legacy.write(fs->legacy.devId, bno * fs->blocksz, buff, size) != size) {
			return -EIO;
		}
	}
	else {
		return -ENOSYS;
	}

	return EOK;
}


int ext2_block_destroy(ext2_t *fs, uint32_t bno, uint32_t n)
{
	uint32_t group = (bno - 1) / fs->sb->groupBlocks;
	uint32_t i, j, pgroup = group, offset = 0;
	void *bmp;
	int err;

	if ((bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_block_read(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	for (i = 0, j = 0; i < n; i++, j++) {
		group = (bno + i - 1) / fs->sb->groupBlocks;
		offset = (bno + i - 1) % fs->sb->groupBlocks + 1;

		if (group != pgroup) {
			if ((err = ext2_block_write(fs, fs->gdt[pgroup].blockBmp, bmp, 1)) < 0) {
				free(bmp);
				return err;
			}

			fs->gdt[pgroup].freeBlocks += j;

			if ((err = ext2_gdt_syncone(fs, pgroup)) < 0) {
				for (i = 0; i < j; i++)
					ext2_togglebit(bmp, --offset);

				if (!ext2_block_write(fs, fs->gdt[pgroup].blockBmp, bmp, 1))
					fs->gdt[pgroup].freeBlocks -= j;
				else
					fs->sb->freeBlocks += j;

				free(bmp);
				return err;
			}

			fs->sb->freeBlocks += j;
			pgroup = group;
			j = 0;

			if ((err = ext2_block_read(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
				free(bmp);
				return err;
			}
		}

		ext2_togglebit(bmp, offset);
	}

	if ((err = ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	fs->gdt[group].freeBlocks += j;

	if ((err = ext2_gdt_syncone(fs, group)) < 0) {
		for (i = 0; i < j; i++)
			ext2_togglebit(bmp, offset--);

		if (!ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1))
			fs->gdt[group].freeBlocks -= j;
		else
			fs->sb->freeBlocks += j;

		free(bmp);
		return err;
	}

	fs->sb->freeBlocks += j;
	free(bmp);

	return EOK;
}


/* Allocates one new block */
static int ext2_block_createone(ext2_t *fs, uint32_t bno, uint32_t *res)
{
	uint32_t group = (bno - 1) / fs->sb->groupInodes;
	uint32_t offset, pgroup = group;
	void *bmp;
	int err;

	if ((bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	do {
		if ((err = ext2_block_read(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
			free(bmp);
			return err;
		}

		if (!(offset = ext2_findzerobit(bmp, fs->sb->groupBlocks, 0))) {
			group = (group + 1) % fs->groups;

			if (group == pgroup) {
				free(bmp);
				return -ENOSPC;
			}
		}
	} while (!offset);

	ext2_togglebit(bmp, offset);

	if ((err = ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	fs->gdt[group].freeBlocks--;

	if ((err = ext2_gdt_syncone(fs, group)) < 0) {
		ext2_togglebit(bmp, offset);

		if (!ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1))
			fs->gdt[group].freeBlocks++;
		else
			fs->sb->freeBlocks--;

		free(bmp);
		return err;
	}

	fs->sb->freeBlocks--;
	*res = group * fs->sb->groupBlocks + offset;
	free(bmp);

	return EOK;
}


/* Tries to allocate n consecutive blocks */
static int ext2_block_create(ext2_t *fs, ext2_obj_t *obj, uint32_t block, uint32_t lbno, uint32_t n, uint32_t *res)
{
	uint32_t *bno, group, pgroup, offset, i, j;
	void *bmp;
	int err;

	if ((bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if (lbno) {
		group = (lbno - 1) / fs->sb->groupBlocks;
		offset = (lbno - 1) % fs->sb->groupBlocks + 1;
	}
	else {
		group = ((uint32_t)obj->id - 1) / fs->sb->groupInodes;
		offset = 0;
	}
	pgroup = group;

	do {
		if ((err = ext2_block_read(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
			free(bmp);
			return err;
		}

		if (!offset)
			offset = ext2_findzerobit(bmp, fs->sb->groupBlocks, 0);
		else if (!ext2_checkbit(bmp, offset))
			break;
		else
			offset = ext2_findzerobit(bmp, fs->sb->groupBlocks, offset);

		if (!offset) {
			group = (group + 1) % fs->groups;

			if (group == pgroup) {
				free(bmp);
				return -ENOSPC;
			}
		}
	} while (!offset);

	for (i = 0; (i < n) && (offset + i < fs->sb->groupBlocks) && !ext2_checkbit(bmp, offset + i); i++) {
		if ((err = ext2_block_get(fs, obj, block + i, &bno)) < 0) {
			free(bmp);
			return err;
		}

		ext2_togglebit(bmp, offset + i);
		*bno = group * fs->sb->groupBlocks + offset + i;
	}

	if ((err = ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	fs->gdt[group].freeBlocks -= i;

	if ((err = ext2_gdt_syncone(fs, group)) < 0) {
		for (j = 0; j < i; j++)
			ext2_togglebit(bmp, offset + j);

		if (!ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1))
			fs->gdt[group].freeBlocks += i;
		else
			fs->sb->freeBlocks -= i;

		free(bmp);
		return err;
	}

	fs->sb->freeBlocks -= i;
	*res = i;
	free(bmp);

	return EOK;
}


/* Calculates block indirection offsets */
static int ext2_block_offs(ext2_t *fs, uint32_t block, uint32_t offs[4])
{
	uint32_t addr = 256 << fs->sb->logBlocksz;
	uint32_t bits = 8 + fs->sb->logBlocksz;
	int depth = 0;

	if (block < DIRECT_BLOCKS) {
		offs[depth++] = block;
	}
	else if ((block -= DIRECT_BLOCKS) < addr) {
		offs[depth++] = block;
		offs[depth++] = SINGLE_INDIRECT_BLOCK;
	}
	else if ((block -= addr) < addr << bits) {
		offs[depth++] = block & (addr - 1);
		offs[depth++] = block >> bits;
		offs[depth++] = DOUBLE_INDIRECT_BLOCK;
	}
	else if ((block -= addr << bits) >> (2 * bits) < addr) {
		offs[depth++] = block & (addr - 1);
		offs[depth++] = (block >> bits) & (addr - 1);
		offs[depth++] = block >> (2 * bits);
		offs[depth++] = TRIPPLE_INDIRECT_BLOCK;
	}
	else {
		return -EINVAL;
	}

	return depth;
}


/* Reads an indirect block */
static int ext2_block_readind(ext2_t *fs, ext2_obj_t *obj, uint32_t *bno, int depth, uint32_t **ind)
{
	int err;

	depth -= 2;

	if (!(*bno) || (*bno != obj->ind[depth].bno)) {
		if (obj->ind[depth].data == NULL) {
			if ((obj->ind[depth].data = (uint32_t *)malloc(fs->blocksz)) == NULL)
				return -ENOMEM;
		}
		else {
			if ((err = ext2_block_write(fs, obj->ind[depth].bno, obj->ind[depth].data, 1)) < 0)
				return err;
		}

		if (!(*bno)) {
			if ((err = ext2_block_createone(fs, (uint32_t)obj->id, &obj->ind[depth].bno)) < 0)
				return err;

			memset(obj->ind[depth].data, 0, fs->blocksz);
			*bno = obj->ind[depth].bno;
		}
		else {
			if ((err = ext2_block_read(fs, *bno, obj->ind[depth].data, 1)) < 0)
				return err;

			obj->ind[depth].bno = *bno;
		}
	}

	*ind = obj->ind[depth].data;

	return EOK;
}


/* Reads indirect blocks */
static int ext2_block_ind(ext2_t *fs, ext2_obj_t *obj, int depth, uint32_t offs[4], uint32_t *ind[3])
{
	int err;

	if (depth == 4) {
		if ((err = ext2_block_readind(fs, obj, obj->inode->block + offs[3], depth, ind + 2)) < 0)
			return err;
	}

	if (depth >= 3) {
		if (depth == 4) {
			if ((err = ext2_block_readind(fs, obj, ind[2] + offs[2], --depth, ind + 1)) < 0)
				return err;
		}
		else {
			if ((err = ext2_block_readind(fs, obj, obj->inode->block + offs[2], depth, ind + 1)) < 0)
				return err;
		}
	}

	if (depth >= 2) {
		if (depth == 3) {
			if ((err = ext2_block_readind(fs, obj, ind[1] + offs[1], --depth, ind)) < 0)
				return err;
		}
		else {
			if ((err = ext2_block_readind(fs, obj, obj->inode->block + offs[1], depth, ind)) < 0)
				return err;
		}
	}

	return EOK;
}


int ext2_block_get(ext2_t *fs, ext2_obj_t *obj, uint32_t block, uint32_t **res)
{
	uint32_t *ind[3] = { NULL, NULL, NULL };
	uint32_t offs[4] = { 0 };
	int err, depth;

	if ((depth = ext2_block_offs(fs, block, offs)) < 0)
		return depth;

	if (depth > 1) {
		if ((err = ext2_block_ind(fs, obj, depth, offs, ind)) < 0)
			return err;

		*res = ind[0] + offs[0];
	}
	else {
		*res = obj->inode->block + offs[0];
	}

	return EOK;
}


int ext2_block_syncone(ext2_t *fs, ext2_obj_t *obj, uint32_t block, const void *buff)
{
	uint32_t *bno;
	int err;

	if ((err = ext2_block_get(fs, obj, block, &bno)) < 0)
		return err;

	if (!(*bno)) {
		if ((err = ext2_block_createone(fs, (uint32_t)obj->id, &block)) < 0)
			return err;

		*bno = block;
		obj->inode->blocks += fs->blocksz / fs->sectorsz;
	}

	return ext2_block_write(fs, *bno, buff, 1);
}


int ext2_block_sync(ext2_t *fs, ext2_obj_t *obj, uint32_t block, const void *buff, uint32_t n)
{
	uint32_t lbno = 0, i = 0, j = 0, k = 0;
	uint32_t *bno;
	int err;

	while (j < n) {
		if ((err = ext2_block_get(fs, obj, block + j, &bno)) < 0)
			return err;

		if (!(*bno)) {
			if (i < j) {
				if ((err = ext2_block_get(fs, obj, i, &bno)) < 0)
					return err;

				if ((err = ext2_block_write(fs, *bno, buff + i * fs->blocksz, j - i)) < 0)
					return err;
			}

			for (i = j++; j < n; j++) {
				if ((err = ext2_block_get(fs, obj, block + j, &bno)) < 0)
					return err;

				if (*bno)
					break;
			}

			for (; i < j; i += k) {
				if ((err = ext2_block_create(fs, obj, block + i, lbno, j - i, &k)) < 0)
					return err;

				if ((err = ext2_block_get(fs, obj, block + i, &bno)) < 0)
					return err;

				if ((err = ext2_block_write(fs, *bno, buff + i * fs->blocksz, k)) < 0)
					return err;
			}

			lbno = 0;
		}
		else if (!lbno || (*bno == lbno + 1)) {
			lbno = *bno;
			j++;
		}
		else {
			if ((err = ext2_block_get(fs, obj, block + i, &bno)) < 0)
				return err;

			if ((err = ext2_block_write(fs, *bno, buff + i * fs->blocksz, j - i)) < 0)
				return err;

			lbno = 0;
			i = j++;
		}
	}

	if (i < j) {
		if ((err = ext2_block_get(fs, obj, block + i, &bno)) < 0)
			return err;

		if ((err = ext2_block_write(fs, *bno, buff + i * fs->blocksz, j - i)) < 0)
			return err;
	}

	return EOK;
}


/* Destroys a block */
static int ext2_block_destroyone(ext2_t *fs, uint32_t bno)
{
	uint32_t group = (bno - 1) / fs->sb->groupBlocks;
	uint32_t offset = (bno - 1) % fs->sb->groupBlocks + 1;
	void *bmp;
	int err;

	if (!bno)
		return EOK;

	if ((bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_block_read(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	ext2_togglebit(bmp, offset);

	if ((err = ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1)) < 0) {
		free(bmp);
		return err;
	}

	fs->gdt[group].freeBlocks++;

	if ((err = ext2_gdt_syncone(fs, group)) < 0) {
		ext2_togglebit(bmp, offset);

		if (!ext2_block_write(fs, fs->gdt[group].blockBmp, bmp, 1))
			fs->gdt[group].freeBlocks--;
		else
			fs->sb->freeBlocks++;

		free(bmp);
		return err;
	}

	fs->sb->freeBlocks++;
	free(bmp);

	return EOK;
}


int ext2_iblock_destroy(ext2_t *fs, ext2_obj_t *obj, uint32_t block, uint32_t n)
{
	uint32_t *ind[3] = { NULL, NULL, NULL };
	uint32_t i, offs[4] = { 0 };
	int err, depth;

	for (i = 0; i < n; i++) {
		if ((depth = ext2_block_offs(fs, block + i, offs)) < 0)
			return depth;

		if ((err = ext2_block_ind(fs, obj, depth, offs, ind)) < 0)
			return err;

		if (ind[0] != NULL)
			*(ind[0] + offs[0]) = 0;

		switch (depth) {
		case 1:
			obj->inode->block[offs[0]] = 0;
			break;

		case 2:
			if (!offs[0]) {
				if ((err = ext2_block_destroyone(fs, obj->inode->block[offs[1]])) < 0)
					return err;

				obj->inode->block[offs[1]] = 0;
			}
			break;

		case 3:
			if (!offs[0]) {
				if ((err = ext2_block_destroyone(fs, *(ind[1] + offs[1]))) < 0)
					return err;

				*(ind[1] + offs[1]) = 0;
			}

			if (!offs[1]) {
				if ((err = ext2_block_destroyone(fs, obj->inode->block[offs[2]])) < 0)
					return err;

				obj->inode->block[offs[2]] = 0;
			}
			break;

		case 4:
			if (!offs[0]) {
				if ((err = ext2_block_destroyone(fs, *(ind[1] + offs[1]))) < 0)
					return err;

				*(ind[1] + offs[1]) = 0;
			}

			if (!offs[1]) {
				if ((err = ext2_block_destroyone(fs, *(ind[2] + offs[2]))) < 0)
					return err;

				*(ind[2] + offs[2]) = 0;
			}

			if (!offs[2]) {
				if ((err = ext2_block_destroyone(fs, obj->inode->block[offs[3]])) < 0)
					return err;

				obj->inode->block[offs[3]] = 0;
			}
			break;
		}
	}

	return EOK;
}


int ext2_block_init(ext2_t *fs, ext2_obj_t *obj, uint32_t block, void *buff)
{
	uint32_t *bno;
	int err;

	if ((err = ext2_block_get(fs, obj, block, &bno)) < 0)
		return err;

	return ext2_block_read(fs, *bno, buff, 1);
}
