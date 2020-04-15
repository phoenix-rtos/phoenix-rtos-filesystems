/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * block.c
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
#include <string.h>
#include <errno.h>
#include <sys/msg.h>

#include "ext2.h"
#include "block.h"
#include "sb.h"


int write_block(ext2_fs_info_t *f, uint32_t block, const void *data)
{
	ssize_t ret;

	if(!block || data == NULL)
		return EOK;

	if ((ret = f->write(&f->devId, block_offset(f, block), data, f->block_size)) != f->block_size)
		return (int)ret;

	return EOK;
}


int write_blocks(ext2_fs_info_t *f, uint32_t start_block, uint32_t count, const void *data)
{
	ssize_t ret;

	if(!start_block)
		return EOK;

	if ((ret = f->write(&f->devId, block_offset(f, start_block), data, f->block_size * count)) != f->block_size * count)
		return (int)ret;

	return EOK;
}


int read_block(ext2_fs_info_t *f, uint32_t block, void *data)
{
	ssize_t ret;

	if(!block) {
		memset(data, 0, f->block_size);
		return EOK;
	}

	if ((ret = f->read(&f->devId, block_offset(f, block), data, f->block_size)) != (ssize_t)(f->block_size))
		return (int)ret;

	return EOK;
}


/* reads from starting block */
int read_blocks(ext2_fs_info_t *f, uint32_t start_block, uint32_t count, void *data)
{
	ssize_t ret;

	if(!start_block) {
		memset(data, 0, f->block_size * count);
		return EOK;
	}

	if ((ret = f->read(&f->devId, block_offset(f, start_block), data, f->block_size * count)) != f->block_size * count)
		return (int)ret;

	return EOK;
}


/* search block for a given file name */
int search_block(ext2_fs_info_t *f, void *data, const char *name, uint8_t len, id_t *resId)
{
	ext2_dir_entry_t *entry;
	int off = 0;

	while (off < f->block_size) {
		entry = data + off;

		if (entry->rec_len == 0)
			return -ENOENT;
		if (len == entry->name_len
				&& !strncmp(name, entry->name, len)) {
			*resId = entry->inode;
			return EOK;
		}
		off += entry->rec_len;
	}
	return -ENOENT;
}


/* calculates the offs inside indirection blocks, returns depth of the indirection */
static inline int block_off(ext2_fs_info_t *f, long block_no, uint32_t offs[4])
{
	int addr = 256 << f->sb->log_block_size;
	int addr_bits = 8 + f->sb->log_block_size;
	int n = 0;
	long dir_blocks = EXT2_NDIR_BLOCKS;
	long ind_blocks = addr;
	long dind_blocks = addr << addr_bits;
	if (block_no < 0)
		return -EINVAL;
	else if (block_no < dir_blocks) {
		offs[n++] = block_no;
	} else if ((block_no -= dir_blocks) < ind_blocks) {
		offs[n++] = block_no;
		offs[n++] = EXT2_IND_BLOCK;
	} else if ((block_no -= ind_blocks) < dind_blocks) {
		offs[n++] = block_no & (addr - 1);
		offs[n++] = block_no >> addr_bits;
		offs[n++] = EXT2_DIND_BLOCK;
	} else if ((block_no -= dind_blocks) >= 0) {
		offs[n++] = block_no & (addr - 1);
		offs[n++] = (block_no >> addr_bits) & (addr - 1);
		offs[n++] = block_no >> (addr_bits * 2);
		offs[n++] = EXT2_TIND_BLOCK;
	}
	return n;
}


static uint32_t new_block(ext2_fs_info_t *f, uint32_t ino, ext2_inode_t *inode, uint32_t bno)
{
	int group = 0, pgroup = 0;
	uint32_t off = 0;
	void *block_bmp = malloc(f->block_size);

	if (bno) {
		group = (bno - 1) / f->blocks_in_group;
		off = ((bno - 1) % f->blocks_in_group) + 1;
	}
	else
		group = (ino - 1) / f->inodes_in_group;

	pgroup = group;
	do {
		read_block(f, f->gdt[group].block_bitmap, block_bmp);

		if (!off)
			off = find_zero_bit(block_bmp, f->blocks_in_group, 0);
		else {
			if (!check_bit(block_bmp, off))
				break;
			off = find_zero_bit(block_bmp, f->blocks_in_group, off);
		}

		if (off <= 0 || off > f->blocks_in_group) {
			group = (group + 1) % f->gdt_size;
			if (group == pgroup) {
				free(block_bmp);
				return 0;
			}
		}
	} while (off <= 0);

	toggle_bit(block_bmp, off);
	write_block(f, f->gdt[group].block_bitmap, block_bmp);
	f->sb->free_blocks_count--;
	f->gdt[group].free_blocks_count--;
	gdt_sync(f, group);
	free(block_bmp);
	return group * f->blocks_in_group + off;
}


static inline uint32_t *block_read_ind(ext2_object_t *o, uint32_t *bno, int depth)
{
	depth -= 2;
	if (depth < 0 || depth > 2)
		return NULL;
	if (!(*bno) || (*bno) != o->ind[depth].bno) {
		if (o->ind[depth].data == NULL)
			o->ind[depth].data = malloc(o->f->block_size);
		else
			write_block(o->f, o->ind[depth].bno, o->ind[depth].data);

		read_block(o->f, (*bno), o->ind[depth].data);
		if (!(*bno)) {
			o->ind[depth].bno = new_block(o->f, o->id, o->inode, 0);
			*bno = o->ind[depth].bno;
		}
		else
			o->ind[depth].bno = *bno;
	}

	return o->ind[depth].data;
}


inline uint32_t get_block_no(ext2_object_t *o, uint32_t block)
{
	uint32_t *tind, *dind, *ind;
	uint32_t offs[4] = { 0 };
	uint32_t ret;
	int depth = block_off(o->f, block, offs);

	if (depth == 4)
		 tind = block_read_ind(o, &o->inode->block[offs[3]], depth);
	if (depth >= 3) {
		if (depth == 4)
			dind = block_read_ind(o, (tind + offs[2]), --depth);
		else
			dind = block_read_ind(o, &o->inode->block[offs[2]], depth);
	}
	if (depth >= 2) {
		if (depth == 3)
			ind = block_read_ind(o, (dind + offs[1]), --depth);
		else {
			ind = block_read_ind(o, &o->inode->block[offs[1]], depth);
		}
	}

	ret = depth > 1 ? *(ind + offs[0]) : o->inode->block[offs[0]];
	return ret;
}


/* gets block of a given number (relative to inode) */
void get_block(ext2_object_t *o, uint32_t block, void *data)
{
	uint32_t *tind, *dind, *ind;
	uint32_t offs[4] = { 0 };
	int depth = block_off(o->f, block, offs);

	if (depth == 4) {
		tind = block_read_ind(o, &o->inode->block[offs[3]], depth);
	}
	if (depth >= 3) {
		if (depth == 4)
			dind = block_read_ind(o, (tind + offs[2]), --depth);
		else
			dind = block_read_ind(o, &o->inode->block[offs[2]], depth);
	}
	if (depth >= 2) {
		if (depth == 3)
			ind = block_read_ind(o, (dind + offs[1]), --depth);
		else
			ind = block_read_ind(o, &o->inode->block[offs[1]], depth);
	}
	if (depth > 1)
		read_block(o->f, *(ind + offs[0]), data);
	else
		read_block(o->f, o->inode->block[offs[0]], data);
}

void free_block(ext2_fs_info_t *f, uint32_t bno)
{
	uint32_t group;
	uint32_t off;
	void *block_bmp = malloc(f->block_size);

	if (bno == 0)
		return;

	group = (bno - 1) / f->blocks_in_group;
	off = ((bno - 1)  % f->blocks_in_group) + 1;

	//printf("free_block %u\n", bno);
	read_block(f, f->gdt[group].block_bitmap, block_bmp);
	toggle_bit(block_bmp, off);
	write_block(f, f->gdt[group].block_bitmap, block_bmp);
	f->sb->free_blocks_count++;
	f->gdt[group].free_blocks_count++;
	gdt_sync(f, group);

	free(block_bmp);
}


void free_blocks(ext2_fs_info_t *f, uint32_t start, uint32_t count)
{
	uint32_t group;
	uint32_t off;
	uint32_t left = count;
	uint32_t current = start;
	void *block_bmp = malloc(f->block_size);

	group = (start - 1) / f->blocks_in_group;
	off = ((start - 1)  % f->blocks_in_group) + 1;

	read_block(f, f->gdt[group].block_bitmap, block_bmp);

	while (left) {

		if (current > f->blocks_in_group) {
			write_block(f, f->gdt[group].block_bitmap, block_bmp);
			group = (current - 1) / f->blocks_in_group;
			off = ((current - 1)  % f->blocks_in_group) + 1;
			read_block(f, f->gdt[group].block_bitmap, block_bmp);
		}

		toggle_bit(block_bmp, off + (count - left));
		f->sb->free_blocks_count++;
		f->gdt[group].free_blocks_count++;
		left--;
		current++;
	}

	write_block(f, f->gdt[group].block_bitmap, block_bmp);
	ext2_write_sb(f);
	gdt_sync(f, group);
	free(block_bmp);
}


int free_inode_blocks(ext2_object_t *o, uint32_t start, uint32_t count)
{
	uint32_t *tind, *dind, *ind = NULL;
	uint32_t offs[4] = { 0 };
	uint32_t current = start;
	uint32_t left = count;
	int depth = 0;
	while (left) {
		depth = block_off(o->f, current, offs);

		if (depth == 4)
			tind = block_read_ind(o, &o->inode->block[offs[3]], depth);
		if (depth >= 3) {
			if (depth == 4)
				dind = block_read_ind(o, (tind + offs[2]), --depth);
			else
				dind = block_read_ind(o, &o->inode->block[offs[2]], depth);
		}
		if (depth >= 2) {
			if (depth == 3)
				ind = block_read_ind(o, (dind + offs[1]), --depth);
			else
				ind = block_read_ind(o, &o->inode->block[offs[1]], depth);
		}

		if (ind != NULL)
			*(ind + offs[0]) = 0;

		if (depth == 1) {
			o->inode->block[offs[0]] = 0;

		} else if (depth == 2) {
			if (!offs[0]) {
				free_block(o->f, o->inode->block[offs[1]]);
				o->inode->block[offs[1]] = 0;
			}
		} else if (depth == 3) {
			if (!offs[0]) {
				free_block(o->f, *(dind + offs[1]));
				*(dind + offs[1]) = 0;
			}
			if (!offs[1]) {
				free_block(o->f, o->inode->block[offs[2]]);
				o->inode->block[offs[2]] = 0;
			}
		} else if (depth == 4) {
			if (!offs[0]) {
				free_block(o->f, *(dind + offs[1]));
				*(dind + offs[1]) = 0;
			}
			if (!offs[1]) {
				free_block(o->f, *(tind + offs[2]));
				*(tind + offs[2]) = 0;
			}
			if (!offs[2]) {
				free_block(o->f, o->inode->block[offs[3]]);
				o->inode->block[offs[3]] = 0;
			}
		}
		left--;
		current--;
	}

	return EOK;
}

/* sets block of a given number (relative to inode) */
void set_block(ext2_object_t *o, uint32_t block, const void *data)
{
	uint32_t *tind, *dind, *ind;
	uint32_t offs[4] = { 0 };
	int depth = block_off(o->f, block, offs);

	if (depth == 4)
		tind = block_read_ind(o, &o->inode->block[offs[3]], depth);

	if (depth >= 3) {
		if (depth == 4)
			dind = block_read_ind(o, (tind + offs[2]), --depth);
		else
			dind = block_read_ind(o, &o->inode->block[offs[2]], depth);
	}

	if (depth >= 2) {
		if (depth == 3)
			ind = block_read_ind(o, (dind + offs[1]), --depth);
		else
			ind = block_read_ind(o, &o->inode->block[offs[1]], depth);
	}

	if (depth > 1) {
		if (!*(ind + offs[0])) {
			*(ind + offs[0]) = new_block(o->f, o->id, o->inode, 0);
			o->inode->blocks += o->f->block_size / 512;
		}
		write_block(o->f, *(ind + offs[0]), data);
	} else {
		if(!o->inode->block[offs[0]]) {
			o->inode->block[offs[0]] = new_block(o->f, o->id, o->inode, 0);
			o->inode->blocks += o->f->block_size / 512;
		}
		write_block(o->f, o->inode->block[offs[0]], data);
	}
}


/* tries to allocate goal number of consecutive blocks */
uint32_t alloc_blocks(ext2_object_t *o, uint32_t start_block, uint32_t goal, uint32_t bno)
{
	uint32_t count = 1;
	int group = 0, pgroup = 0;
	uint32_t off = 0;
	uint32_t offs[4] = { 0 };
	uint32_t *tind, *dind, *ind;
	uint32_t end_block, current_block;
	int depth;
	void *block_bmp = malloc(o->f->block_size);

	if (bno) {
		group = (bno - 1) / o->f->blocks_in_group;
		off = ((bno - 1) % o->f->blocks_in_group) + 1;
	}
	else
		group = (o->id - 1) / o->f->inodes_in_group;

	pgroup = group;
	do {
		read_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);

		if (!off)
			off = find_zero_bit(block_bmp, o->f->blocks_in_group, 0);
		else {
			if (!check_bit(block_bmp, off))
				break;
			off = find_zero_bit(block_bmp, o->f->blocks_in_group, off);
		}

		if (off <= 0 || off > o->f->blocks_in_group) {
			group = (group + 1) % o->f->gdt_size;
			if (group == pgroup) {
				free(block_bmp);
				return 0;
			}
		}
	} while (off <= 0);

	bno = group * o->f->blocks_in_group + off;
	toggle_bit(block_bmp, off);
	o->f->sb->free_blocks_count--;
	while (count < goal && off < o->f->blocks_in_group && !check_bit(block_bmp, ++off)) {
		toggle_bit(block_bmp, off);
		o->f->sb->free_blocks_count--;
		o->f->gdt[group].free_blocks_count--;
		count++;
	}

	end_block = start_block + count;

	current_block = start_block;

	/* fill inode block array with allocated blocks */
	while (current_block < end_block) {
		depth = block_off(o->f, current_block, offs);
		if (depth == 1)
			o->inode->block[offs[0]] = bno++;
		else if (depth == 2) {

			if (!o->inode->block[offs[1]]) {
				write_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);
				ind = block_read_ind(o, &o->inode->block[offs[1]], depth);
				read_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);
			} else ind = block_read_ind(o, &o->inode->block[offs[1]], depth);

			*(ind + offs[0]) = bno++;
		} else if (depth == 3) {

			write_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);
			dind = block_read_ind(o, &o->inode->block[offs[2]], depth);
			ind = block_read_ind(o, (dind + offs[1]), --depth);
			read_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);

			*(ind + offs[0]) = bno++;
		} else if (depth == 4) {

			write_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);
			tind = block_read_ind(o, &o->inode->block[offs[3]], depth);
			dind = block_read_ind(o, (tind + offs[2]), --depth);
			ind = block_read_ind(o, (dind + offs[1]), --depth);
			read_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);

			*(ind + offs[0]) = bno++;
		}

		current_block++;
	}

	write_block(o->f, o->f->gdt[group].block_bitmap, block_bmp);
	gdt_sync(o->f, group);
	free(block_bmp);
	return count;
}

int set_blocks(ext2_object_t *o, uint32_t start_block, uint32_t count, const void *data)
{
	uint32_t block = start_block;
	uint32_t checkpoint = start_block;
	uint32_t last, current;

	last = 0;

	while (block < start_block + count) {
		current = get_block_no(o, block);
		if (!current) {
			if (checkpoint != block)
				write_blocks(o->f, get_block_no(o, checkpoint), block - checkpoint,
						data + ((checkpoint - start_block) * o->f->block_size));

			checkpoint = block;
			block++;
			while(block < start_block + count && !get_block_no(o, block))
				block++;

			/* alloc and write blocks */
			while (checkpoint != block) {
				last = alloc_blocks(o, checkpoint, block - checkpoint, last);

				if (!last)
					return -ENOSPC; //no free blocks on disk

				write_blocks(o->f, get_block_no(o, checkpoint), last,
						data + ((checkpoint - start_block) * o->f->block_size));
				checkpoint += last;
			}

			last = 0;
		} else if (current == last + 1 || last == 0) {
			last = current;
			block++;
		} else {
			if (checkpoint != block)
				write_blocks(o->f, get_block_no(o, checkpoint), block - checkpoint,
						data + ((checkpoint - start_block) * o->f->block_size));

			checkpoint = block;
			last = 0;
			block++;
		}
	}
	if (checkpoint != block)
		write_blocks(o->f, get_block_no(o, checkpoint), block - checkpoint,
				data + ((checkpoint - start_block) * o->f->block_size));

	return EOK;
}
