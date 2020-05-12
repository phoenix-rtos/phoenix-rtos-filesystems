/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Block
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


int ext2_read_blocks(ext2_t *fs, uint32_t start, uint32_t blocks, void *data)
{
	ssize_t ret, size = blocks * fs->blocksz;

	if(!start)
		memset(data, 0, size);
	else if ((ret = fs->read(fs->dev, start * fs->blocksz, data, size)) != size)
		return (int)ret;

	return EOK;
}


int ext2_write_blocks(ext2_t *fs, uint32_t start, uint32_t blocks, const void *data)
{
	ssize_t ret, size = blocks * fs->blocksz;

	if (start && (ret = fs->write(fs->dev, start * fs->blocksz, data, size)) != size)
		return (int)ret;

	return EOK;
}


static int ext2_create_block(ext2_t *fs, uint32_t ino, uint32_t *res)
{
	void *block_bmp;
	int err;
	uint32_t group = (ino - 1) / fs->sb->group_inodes;
	uint32_t offs, pgroup = group;

	if ((block_bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	do {
		if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
			free(block_bmp);
			return err;
		}

		if (!(offs = ext2_find_zero_bit(block_bmp, fs->sb->group_blocks, 0))) {
			group = (group + 1) % fs->groups;

			if (group == pgroup) {
				free(block_bmp);
				return -ENOSPC;
			}
		}
	} while (!offs);

	ext2_toggle_bit(block_bmp, offs);

	if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
		free(block_bmp);
		return err;
	}

	free(block_bmp);
	fs->gdt[group].free_blocks--;

	if ((err = ext2_sync_gd(fs, group)) < 0)
		return err;

	fs->sb->free_blocks--;
	*res = group * fs->sb->group_blocks + offs;

	return EOK;
}


static int ext2_block_offs(ext2_t *fs, uint32_t block, uint32_t offs[4])
{
	int n = 0;
	uint32_t naddr = 256 << fs->sb->log_blocksz;
	uint32_t bits = 8 + fs->sb->log_blocksz;

	if (block < DIRECT_BLOCKS) {
		offs[n++] = block;
	}
	else if ((block -= DIRECT_BLOCKS) < naddr) {
		offs[n++] = block;
		offs[n++] = SINGLE_INDIRECT_BLOCK;
	}
	else if ((block -= naddr) < naddr << bits) {
		offs[n++] = block & (naddr - 1);
		offs[n++] = block >> bits;
		offs[n++] = DOUBLE_INDIRECT_BLOCK;
	}
	else if ((block -= naddr << bits) >> (2 * bits) < naddr) {
		offs[n++] = block & (naddr - 1);
		offs[n++] = (block >> bits) & (naddr - 1);
		offs[n++] = block >> (2 * bits);
		offs[n++] = TRIPPLE_INDIRECT_BLOCK;
	}
	else {
		return -EINVAL;
	}

	return n;
}


static int ext2_read_ind_block(ext2_t *fs, ext2_obj_t *obj, uint32_t *block, int depth, uint32_t **ind)
{
	int err;

	depth -= 2;

	if (depth < 0 || depth > 2) {
		*ind = NULL;
		return EOK;
	}

	if (!(*block) || *block != obj->ind[depth].block) {
		if (obj->ind[depth].data == NULL) {
			if ((obj->ind[depth].data = (uint32_t *)malloc(fs->blocksz)) == NULL)
				return -ENOMEM;
		}
		else {
			if ((err = ext2_write_blocks(fs, obj->ind[depth].block, 1, obj->ind[depth].data)) < 0)
				return err;
		}

		if ((err = ext2_read_blocks(fs, *block, 1, obj->ind[depth].data)) < 0)
			return err;

		if (!(*block)) {
			if ((err = ext2_create_block(fs, (uint32_t)obj->ino, &(obj->ind[depth].block))) < 0)
				return err;

			*block = obj->ind[depth].block;
		}
		else
			obj->ind[depth].block = *block;
	}

	*ind = obj->ind[depth].data;

	return EOK;
}


static int ext2_get_blockptr(ext2_t *fs, ext2_obj_t *obj, uint32_t block, uint32_t **ptr)
{
	int err, depth;
	uint32_t *ind, *dind, *tind;
	uint32_t offs[4] = { 0 };

	if ((depth = ext2_block_offs(fs, block, offs)) < 0)
		return depth;

	if (depth == 4) {
		if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[3]], depth, &tind)) < 0)
			return err;
	}

	if (depth >= 3) {
		if (depth == 4) {
			if ((err = ext2_read_ind_block(fs, obj, tind + offs[2], --depth, &dind)) < 0)
				return err;
		}
		else {
			if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[2]], depth, &dind)) < 0)
				return err;
		}
	}

	if (depth >= 2) {
		if (depth == 3) {
			if ((err = ext2_read_ind_block(fs, obj, dind + offs[1], --depth, &ind)) < 0)
				return err;
		}
		else {
			if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[1]], depth, &ind)) < 0)
				return err;
		}
	}

	*ptr = (depth > 1) ? ind + offs[0] : obj->inode->block + offs[0];

	return EOK;
}


int ext2_get_block(ext2_t *fs, ext2_obj_t *obj, uint32_t block, uint32_t *res)
{
	uint32_t *ptr;
	int err;

	if ((err = ext2_get_blockptr(fs, obj, block, &ptr)) < 0)
		return err;

	*res = *ptr;

	return EOK;
}


int ext2_init_block(ext2_t *fs, ext2_obj_t *obj, uint32_t block, void *data)
{
	int err;

	if ((err = ext2_get_block(fs, obj, block, &block)) < 0)
		return err;

	return ext2_read_blocks(fs, block, 1, data);
}


int ext2_sync_block(ext2_t *fs, ext2_obj_t *obj, uint32_t block, const void *data)
{
	uint32_t *ptr;
	int err;

	if ((err = ext2_get_blockptr(fs, obj, block, &ptr)) < 0)
		return err;

	if (!(*ptr)) {
		if ((err = ext2_create_block(fs, (uint32_t)obj->ino, ptr)) < 0)
			return err;

		obj->inode->blocks += fs->blocksz / fs->sectorsz;
	}

	if ((err = ext2_write_blocks(fs, *ptr, 1, data)) < 0)
		return err;

	return EOK;
}


static int ext2_alloc_blocks(ext2_t *fs, ext2_obj_t *obj, uint32_t start, uint32_t goal, uint32_t block, uint32_t *res)
{
	void *block_bmp;
	int err, depth;
	uint32_t group, pgroup, offset, curr, end;
	uint32_t *ind, *dind, *tind;
	uint32_t offs[4] = { 0 };

	if ((block_bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if (block) {
		group = (block - 1) / fs->sb->group_blocks;
		offset = (block - 1) % obj->fs->sb->group_blocks + 1;
	}
	else {
		group = ((uint32_t)obj->ino - 1) / fs->sb->group_inodes;
		offset = 0;
	}
	pgroup = group;

	do {
		if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
			free(block_bmp);
			return err;
		}

		if (!offset)
			offset = ext2_find_zero_bit(block_bmp, fs->sb->group_blocks, 0);
		else if (!ext2_check_bit(block_bmp, offset))
			break;
		else
			offset = ext2_find_zero_bit(block_bmp, fs->sb->group_blocks, offset);

		if (!offset) {
			group = (group + 1) % fs->groups;

			if (group == pgroup) {
				free(block_bmp);
				return -ENOSPC;
			}
		}
	} while (!offset);

	block = group * fs->sb->group_blocks + offset;
	ext2_toggle_bit(block_bmp, offset);
	fs->gdt[group].free_blocks--;
	fs->sb->free_blocks--;
	*res = 1;

	while (*res < goal && offset < fs->sb->group_blocks && !ext2_check_bit(block_bmp, ++offset)) {
		ext2_toggle_bit(block_bmp, offset);
		fs->gdt[group].free_blocks--;
		fs->sb->free_blocks--;
		(*res)++;
	}

	curr = start;
	end = start + *res;

	/* Fill inode block array with allocated blocks */
	while (curr < end) {
		if ((depth = ext2_block_offs(fs, curr, offs)) < 0) {
			free(block_bmp);
			return depth;
		}

		if (depth == 1) {
			obj->inode->block[offs[0]] = block++;
		}
		else if (depth == 2) {
			if (!obj->inode->block[offs[1]]) {
				if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
					free(block_bmp);
					return err;
				}

				if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[1]], depth, &ind)) < 0) {
					free(block_bmp);
					return err;
				}

				if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
					free(block_bmp);
					return err;
				}
			}
			else {
				if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[1]], depth, &ind)) < 0) {
					free(block_bmp);
					return err;
				}
			}

			*(ind + offs[0]) = block++;
		}
		else if (depth == 3) {
			if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[2]], depth, &dind)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_ind_block(fs, obj, dind + offs[1], --depth, &ind)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
				free(block_bmp);
				return err;
			}

			*(ind + offs[0]) = block++;
		}
		else if (depth == 4) {
			if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[3]], depth, &tind)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_ind_block(fs, obj, tind + offs[2], --depth, &dind)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_ind_block(fs, obj, dind + offs[1], --depth, &ind)) < 0) {
				free(block_bmp);
				return err;
			}

			if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
				free(block_bmp);
				return err;
			}

			*(ind + offs[0]) = block++;
		}

		curr++;
	}

	if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
		free(block_bmp);
		return err;
	}

	free(block_bmp);

	if ((err = ext2_sync_gd(fs, group)) < 0)
		return err;

	return EOK;
}


int ext2_sync_blocks(ext2_t *fs, ext2_obj_t *obj, uint32_t start, uint32_t blocks, const void *data)
{
	int err;
	uint32_t curr, last = 0;
	uint32_t cpblock, block = start, cp = start;

	while (block < start + blocks) {
		if ((err = ext2_get_block(fs, obj, block, &curr)) < 0)
			return err;

		if (!curr) {
			if (cp != block) {
				if ((err = ext2_get_block(fs, obj, cp, &cpblock)) < 0)
					return err;

				if ((err = ext2_write_blocks(fs, cpblock, block - cp, data + (cp - start) * fs->blocksz)) < 0)
					return err;
			}
			cp = block++;

			while (block < start + blocks) {
				if ((err = ext2_get_block(fs, obj, block, &cpblock)) < 0)
					return err;
				
				if (!cpblock)
					block++;
				else
					break;
			}

			/* Alloc and write blocks */
			while (cp != block) {
				if ((err = ext2_alloc_blocks(fs, obj, cp, block - cp, last, &last)) < 0)
					return err;

				if ((err = ext2_get_block(fs, obj, cp, &cpblock)) < 0)
					return err;

				if ((err = ext2_write_blocks(fs, cpblock, last, data + (cp - start) * fs->blocksz)) < 0)
					return err;

				cp += last;
			}

			last = 0;
		}
		else if (curr == last + 1 || last == 0) {
			last = curr;
			block++;
		}
		else {
			if (cp != block) {
				if ((err = ext2_get_block(fs, obj, cp, &cpblock)) < 0)
					return err;

				if ((err = ext2_write_blocks(fs, cpblock, block - cp, data + (cp - start) * fs->blocksz)) < 0)
					return err;
			}

			last = 0;
			cp = block++;
		}
	}

	if (cp != block) {
		if ((err = ext2_get_block(fs, obj, cp, &cpblock)) < 0)
			return err;

		if ((err = ext2_write_blocks(fs, cpblock, block - cp, data + (cp - start) * fs->blocksz)) < 0)
			return err;
	}

	return EOK;
}


static int ext2_destroy_block(ext2_t *fs, uint32_t block)
{
	void *block_bmp;
	int err;
	uint32_t group = (block - 1) / fs->sb->group_blocks;
	uint32_t offs = (block - 1) % fs->sb->group_blocks + 1;

	if (!block)
		return EOK;

	if ((block_bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
		free(block_bmp);
		return err;
	}

	ext2_toggle_bit(block_bmp, offs);

	if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
		free(block_bmp);
		return err;
	}

	free(block_bmp);
	fs->gdt[group].free_blocks++;

	if ((err = ext2_sync_gd(fs, group)) < 0)
		return err;

	fs->sb->free_blocks++;

	return EOK;
}


int ext2_destroy_blocks(ext2_t *fs, uint32_t start, uint32_t blocks)
{
	void *block_bmp;
	int err;
	uint32_t group = (start - 1) / fs->sb->group_blocks;
	uint32_t offs = (start - 1)  % fs->sb->group_blocks + 1;
	uint32_t left = blocks;
	uint32_t curr = start;

	if ((block_bmp = malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
		free(block_bmp);
		return err;
	}

	while (left) {
		if (curr > fs->sb->group_blocks) {
			if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
				free(block_bmp);
				return err;
			}

			group = (curr - 1) / fs->sb->group_blocks;
			offs = (curr - 1) % fs->sb->group_blocks + 1;

			if ((err = ext2_read_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
				free(block_bmp);
				return err;
			}
		}

		ext2_toggle_bit(block_bmp, offs + (blocks - left));

		fs->gdt[group].free_blocks++;
		fs->sb->free_blocks++;
		curr++;
		left--;
	}

	if ((err = ext2_write_blocks(fs, fs->gdt[group].block_bmp, 1, block_bmp)) < 0) {
		free(block_bmp);
		return err;
	}

	free(block_bmp);

	if ((err = ext2_sync_gd(fs, group)) < 0)
		return err;

	if ((err = ext2_sync_sb(fs)) < 0)
		return err;

	return EOK;
}


int ext2_destroy_iblocks(ext2_t *fs, ext2_obj_t *obj, uint32_t start, uint32_t blocks)
{
	int err, depth;
	uint32_t *ind, *dind, *tind;
	uint32_t offs[4] = { 0 };
	uint32_t curr = start;
	uint32_t left = blocks;

	while (left) {
		if ((depth = ext2_block_offs(fs, curr, offs)) < 0)
			return depth;

		if (depth == 4) {
			if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[3]], depth, &tind)) < 0)
				return err;
		}

		if (depth >= 3) {
			if (depth == 4) {
				if ((err = ext2_read_ind_block(fs, obj, tind + offs[2], --depth, &dind)) < 0)
					return err;
			}
			else {
				if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[2]], depth, &dind)) < 0)
					return err;
			}
		}

		if (depth >= 2) {
			if (depth == 3) {
				if ((err = ext2_read_ind_block(fs, obj, (dind + offs[1]), --depth, &ind)) < 0)
					return err;
			}
			else {
				if ((err = ext2_read_ind_block(fs, obj, &obj->inode->block[offs[1]], depth, &ind)) < 0)
					return err;
			}
		}

		if (ind != NULL)
			*(ind + offs[0]) = 0;

		if (depth == 1) {
			obj->inode->block[offs[0]] = 0;
		}
		else if (depth == 2) {
			if (!offs[0]) {
				if ((err = ext2_destroy_block(fs, obj->inode->block[offs[1]])) < 0)
					return err;

				obj->inode->block[offs[1]] = 0;
			}
		}
		else if (depth == 3) {
			if (!offs[0]) {
				if ((err = ext2_destroy_block(fs, *(dind + offs[1]))) < 0)
					return err;

				*(dind + offs[1]) = 0;
			}

			if (!offs[1]) {
				if ((err = ext2_destroy_block(fs, obj->inode->block[offs[2]])) < 0)
					return err;

				obj->inode->block[offs[2]] = 0;
			}
		}
		else if (depth == 4) {
			if (!offs[0]) {
				if ((err = ext2_destroy_block(fs, *(dind + offs[1]))) < 0)
					return err;

				*(dind + offs[1]) = 0;
			}

			if (!offs[1]) {
				if ((err = ext2_destroy_block(fs, *(tind + offs[2]))) < 0)
					return err;

				*(tind + offs[2]) = 0;
			}

			if (!offs[2]) {
				if ((err = ext2_destroy_block(fs, obj->inode->block[offs[3]])) < 0)
					return err;

				obj->inode->block[offs[3]] = 0;
			}
		}

		curr--;
		left--;
	}

	return EOK;
}
