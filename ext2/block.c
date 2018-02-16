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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/msg.h>

#include "ext2.h"
#include "block.h"
#include "sb.h"
#include "pc-ata.h"


int write_block(u32 block, void *data)
{
	int ret;

	if(!block)
		return EOK;

	ret = ata_write(block_offset(block), data, ext2->block_size);

	if (ret == ext2->block_size)
		return EOK;
	return -EINVAL;
}

int write_blocks(u32 start_block, u32 count, void *data)
{
	int ret;

	if(!start_block)
		return EOK;

	ret = ata_write(block_offset(start_block), data, ext2->block_size * count);

	if (ret == ext2->block_size * count)
		return EOK;
	return -EINVAL;
}


int read_block(u32 block, void *data)
{
	int ret;

	if(!block) {
		memset(data, 0, ext2->block_size);
		return EOK;
	}

	ret = ata_read(block_offset(block), data, ext2->block_size);

	if (ret == ext2->block_size)
		return EOK;
	return ret;
}

/* reads from starting block */
int read_blocks(u32 start_block, u32 count, void *data)
{
	int ret;

	if(!start_block) {
		memset(data, 0, ext2->block_size * count);
		return EOK;
	}

	ret = ata_read(block_offset(start_block), data, ext2->block_size * count);

	if (ret == ext2->block_size)
		return EOK;
	return ret;
}

/* search block for a given file name */
u32 search_block(void *data, const char *name, u8 len)
{
	ext2_dir_entry_t *entry;
	int off = 0;

	while (off < ext2->block_size) {
		entry = data + off;

		if (entry->rec_len == 0)
			return 0;
		if (len == entry->name_len
				&& !strncmp(name, entry->name, len)) {
			return entry->inode;
		}
		off += entry->rec_len;
	}
	return 0;
}

/* calculates the offs inside indirection blocks, returns depth of the indirection */
static int block_off(long block_no, u32 offs[4])
{
	int addr = 256 << ext2->sb->log_block_size;
	int addr_bits = 8 + ext2->sb->log_block_size;
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


u32 get_block_no(ext2_inode_t *inode, u32 block, u32 *buff[3])
{
	u32 offs[4] = { 0 };
	u32 ret;
	int depth = block_off(block, offs);

	if (depth == 4)
		read_block(inode->block[offs[3]], buff[2]);
	if (depth >= 3) {
		if (depth == 4)
			read_block(*(buff[2] + offs[2]), buff[1]);
		else
			read_block(inode->block[offs[2]], buff[1]);
	}
	if (depth >= 2) {
		if (depth >= 3)
			read_block(*(buff[1] + offs[1]), buff[0]);
		else
			read_block(inode->block[offs[1]], buff[0]);

	}

	ret = depth > 1 ? *(buff[0] + offs[0]) : inode->block[offs[0]];
	return ret;
}


void free_block(u32 bno)
{
	u32 group;
	u32 off;
	void *block_bmp = malloc(ext2->block_size);

	if (bno == 0)
		return;

	group = (bno - 1) / ext2->blocks_in_group;
	off = ((bno - 1)  % ext2->blocks_in_group) + 1;

	//printf("free_block %u\n", bno);
	read_block(ext2->gdt[group].block_bitmap, block_bmp);
	toggle_bit(block_bmp, off);
	write_block(ext2->gdt[group].block_bitmap, block_bmp);
	ext2->sb->free_blocks_count++;
	ext2->gdt[group].free_blocks_count++;

	free(block_bmp);
}


int free_inode_block(ext2_inode_t *inode, u32 block, u32 *buff[3])
{
	u32 offs[4];
	int depth = block_off(block, offs);

	if (depth == 4)
		read_block(inode->block[offs[3]], buff[2]);
	if (depth >= 3) {
		if (depth == 4)
			read_block(*(buff[2] + offs[2]), buff[1]);
		else
			read_block(inode->block[offs[2]], buff[1]);
	}
	if (depth >= 2) {
		if (depth >= 3)
			read_block(*(buff[1] + offs[1]), buff[0]);
		else
			read_block(inode->block[offs[1]], buff[0]);
	}


	free_block(*(buff[0] + offs[0]));
	*(buff[0] + offs[0]) = 0;
	if (depth == 1) {
		free_block(inode->block[offs[0]]);
		inode->block[offs[0]] = 0;
	} else if (depth == 2) {
		write_block(inode->block[offs[1]], buff[0]);
		if (!offs[0]) {
			free_block(inode->block[offs[1]]);
			inode->block[offs[1]] = 0;
		}
	} else if (depth == 3) {
		write_block(*(buff[1] + offs[1]), buff[0]);
		if (!offs[0]) {
			*(buff[1] + offs[1]) = 0;
			write_block(inode->block[offs[2]], buff[1]);
			if(!offs[1]) {
				free_block(inode->block[offs[2]]);
				inode->block[offs[2]] = 0;
			}
		}
	} else if (depth == 4) {
		write_block(*(buff[1] + offs[1]), buff[0]);
		if (!offs[0]) {
			*(buff[1] + offs[1]) = 0;
			write_block(*(buff[2] + offs[2]), buff[1]);
			if(!offs[1]) {
				*(buff[2] + offs[2]) = 0;
				write_block(*(buff[2] + offs[2]), buff[1]);
				if(!offs[2]) {
					free_block(inode->block[offs[3]]);
					inode->block[offs[3]] = 0;
				}
			}
		}
	}
	return EOK;
}

u32 new_block(u32 ino, ext2_inode_t *inode, u32 bno)
{
	int group = 0, pgroup = 0;
	u32 off = 0;
	void *block_bmp = malloc(ext2->block_size);

	if (bno) {
		group = (bno - 1) / ext2->blocks_in_group;
		off = ((bno - 1) % ext2->blocks_in_group) + 1;
	}
	else
		group = (ino - 1) / ext2->inodes_in_group;

	pgroup = group;
	do {
		read_block(ext2->gdt[group].block_bitmap, block_bmp);

		if (!off)
			off = find_zero_bit(block_bmp, ext2->blocks_in_group, 0);
		else {
			if (!check_bit(block_bmp, off))
				break;
			off = find_zero_bit(block_bmp, ext2->blocks_in_group, off);
		}

		if (off <= 0 || off > ext2->blocks_in_group) {
			group = (group + 1) % ext2->gdt_size;
			if (group == pgroup) {
				free(block_bmp);
				return 0;
			}
		}
	} while (off <= 0);

	toggle_bit(block_bmp, off);
	write_block(ext2->gdt[group].block_bitmap, block_bmp);
	ext2->sb->free_blocks_count--;
	ext2->gdt[group].free_blocks_count--;
	free(block_bmp);
	return group * ext2->blocks_in_group + off;
}



/* gets block of a given number (relative to inode) */
void get_block(ext2_inode_t *inode, u32 block, void *data,
		u32 offs[4], u32 prev_offs[4], u32 *buff[3])
{
	int depth = block_off(block, offs);

	if (depth == 4 && prev_offs[3] != offs[3]) {
		read_block(inode->block[offs[3]], buff[2]);
		prev_offs[3] = offs[3];
	}
	if (depth >= 3 && prev_offs[2] != offs[2]) {
		if (depth == 4)
			read_block(*(buff[2] + offs[2]), buff[1]);
		else
			read_block(inode->block[offs[2]], buff[1]);
		prev_offs[2] = offs[2];
	}
	if (depth >= 2 && prev_offs[1] != offs[1]) {
		if (depth >= 3)
			read_block(*(buff[1] + offs[1]), buff[0]);
		else
			read_block(inode->block[offs[1]], buff[0]);
		prev_offs[1] = offs[1];
	}
	if (depth > 1)
		read_block(*(buff[0] + offs[0]), data);
	else
		read_block(inode->block[offs[0]], data);

	prev_offs[0] = offs[0];
}


/* sets block of a given number (relative to inode) */
void set_block(u32 ino, ext2_inode_t *inode, u32 block, void *data,
		u32 offs[4], u32 prev_offs[4], u32 *buff[3])
{
	int depth = block_off(block, offs);

	if (depth >= 4 && prev_offs[3] != offs[3]) {
		if(!inode->block[offs[3]]) {
			inode->block[offs[3]] = new_block(ino, inode, 0);
			read_block(0, buff[2]);
		} else read_block(inode->block[offs[3]], buff[2]);
		prev_offs[3] = offs[3];
	}
	if (depth >= 3 && prev_offs[2] != offs[2]) {
		if (depth >= 4) {
			if(!*(buff[2] + offs[2])) {
				*(buff[2] + offs[2]) = new_block(ino, inode, 0);
				write_block(inode->block[offs[3]], buff[2]);
				read_block(0, buff[1]);
			} else read_block(*(buff[2] + offs[2]), buff[1]);
		} else if (!inode->block[offs[2]]) {
			inode->block[offs[2]] = new_block(ino, inode, 0);
			read_block(0, buff[1]);
		} else
			read_block(inode->block[offs[2]], buff[1]);
		prev_offs[2] = offs[2];
	}
	if (depth >= 2 && prev_offs[1] != offs[1]) {
		if (depth >= 3) {
			if(!*(buff[1] + offs[1])) {
				*(buff[1] + offs[1]) = new_block(ino, inode, 0);
				if (depth > 3)
					write_block(*(buff[2] + offs[2]), buff[1]);
				else
					write_block(inode->block[offs[2]], buff[1]);
				read_block(0, buff[0]);
			} else read_block(*(buff[1] + offs[1]), buff[0]);
		} else if(!inode->block[offs[1]]) {
			inode->block[offs[1]] = new_block(ino, inode, 0);
			read_block(0, buff[0]);
		} else
			read_block(inode->block[offs[1]], buff[0]);
		prev_offs[1] = offs[1];
	}

	if (depth > 1) {
		if (!*(buff[0] + offs[0])) {
			*(buff[0] + offs[0]) = new_block(ino, inode, 0);
			if (depth > 2)
				write_block(*(buff[1] + offs[1]), buff[0]);
			else
				write_block(inode->block[offs[1]], buff[0]);
			inode->blocks += ext2->block_size / 512;
		}
		write_block(*(buff[0] + offs[0]), data);
	} else {
		if(!inode->block[offs[0]]) {
			inode->block[offs[0]] = new_block(ino, inode, 0);
			inode->blocks += ext2->block_size / 512;
		}
		write_block(inode->block[offs[0]], data);
	}
	prev_offs[0] = offs[0];
}


/* tries to allocate goal number of consecutive blocks */
u32 alloc_blocks(u32 ino, ext2_inode_t *inode, u32 start_block, u32 goal, u32 bno, u32 *buff[3])
{
	u32 count = 1;
	int group = 0, pgroup = 0;
	u32 off = 0;
	u32 offs[4] = { 0 };
	u32 end_block, current_block;
	void *block_bmp = malloc(ext2->block_size);
	int depth, pdepth = 0xFF;

	if (bno) {
		group = (bno - 1) / ext2->blocks_in_group;
		off = ((bno - 1) % ext2->blocks_in_group) + 1;
	}
	else
		group = (ino - 1) / ext2->inodes_in_group;

	pgroup = group;
	do {
		read_block(ext2->gdt[group].block_bitmap, block_bmp);

		if (!off)
			off = find_zero_bit(block_bmp, ext2->blocks_in_group, 0);
		else {
			if (!check_bit(block_bmp, off))
				break;
			off = find_zero_bit(block_bmp, ext2->blocks_in_group, off);
		}

		if (off <= 0 || off > ext2->blocks_in_group) {
			group = (group + 1) % ext2->gdt_size;
			if (group == pgroup) {
				free(block_bmp);
				return 0;
			}
		}
	} while (off <= 0);

	bno = group * ext2->blocks_in_group + off;
	toggle_bit(block_bmp, off);
	ext2->sb->free_blocks_count--;
	while (count < goal && off < ext2->blocks_in_group && !check_bit(block_bmp, ++off)) {
		toggle_bit(block_bmp, off);
		ext2->sb->free_blocks_count--;
		ext2->gdt[group].free_blocks_count--;
		count++;
	}

	end_block = start_block + count;

	current_block = start_block;
	memset(buff[0], 0, ext2->block_size);
	memset(buff[1], 0, ext2->block_size);
	memset(buff[2], 0, ext2->block_size);

	/* fill inode block array with allocated blocks */
	while (current_block < end_block) {
		depth = block_off(current_block, offs);
		if (depth == 1)
			inode->block[offs[0]] = bno++;
		else if (depth == 2) {

			if (!inode->block[offs[1]]) {
				write_block(ext2->gdt[group].block_bitmap, block_bmp);
				inode->block[offs[1]] = new_block(ino, inode, start_block + count);
				read_block(ext2->gdt[group].block_bitmap, block_bmp);
			} else if (current_block == start_block && inode->block[offs[1]])
				read_block(inode->block[offs[1]], buff[0]);

			*(buff[0] + offs[0]) = bno++;
		} else if (depth == 3) {

			if (depth > pdepth)
				write_block(inode->block[EXT2_IND_BLOCK], buff[0]);

			if (!inode->block[offs[2]]) {
				write_block(ext2->gdt[group].block_bitmap, block_bmp);
				inode->block[offs[2]] = new_block(ino, inode, start_block + count);
				read_block(ext2->gdt[group].block_bitmap, block_bmp);
			} else if (start_block == current_block && inode->block[offs[2]])
				read_block(inode->block[offs[2]], buff[1]);

			if (!offs[0] && offs[1]) {
				write_block(*(buff[1] + offs[1] - 1), buff[0]);
				memset(buff[0], 0, ext2->block_size);
			}

			if (!(*(buff[1] + offs[1]))) {
				write_block(ext2->gdt[group].block_bitmap, block_bmp);
				*(buff[1] + offs[1]) = new_block(ino, inode, start_block + count);
				read_block(ext2->gdt[group].block_bitmap, block_bmp);
			} else if (start_block == current_block || !offs[0])
				read_block(*(buff[1] + offs[1]), buff[0]);

			*(buff[0] + offs[0]) = bno++;
		} else if (depth == 4) {

			if (depth > pdepth)
				write_block(inode->block[EXT2_DIND_BLOCK], buff[0]);

			if (!inode->block[offs[3]]) {
				write_block(ext2->gdt[group].block_bitmap, block_bmp);
				inode->block[offs[3]] = new_block(ino, inode, start_block + count);
				read_block(ext2->gdt[group].block_bitmap, block_bmp);
			} else if (start_block == current_block && inode->block[offs[3]])
				read_block(inode->block[offs[3]], buff[2]);

			if (!offs[1] && offs[2]) {
				write_block(*(buff[2] + offs[2] - 1), buff[1]);
				memset(buff[1], 0, ext2->block_size);
			}

			if (!(*(buff[2] + offs[2]))) {
				write_block(ext2->gdt[group].block_bitmap, block_bmp);
				*(buff[2] + offs[2]) = new_block(ino, inode, start_block + count);
				read_block(ext2->gdt[group].block_bitmap, block_bmp);
			} else if (start_block == current_block || !offs[1])
				read_block(*(buff[2] + offs[2]), buff[1]);

			if (!offs[0] && offs[1]) {
				write_block(*(buff[1] + offs[1] - 1), buff[0]);
				memset(buff[0], 0, ext2->block_size);
			}

			if (!(*(buff[1] + offs[1]))) {
				write_block(ext2->gdt[group].block_bitmap, block_bmp);
				*(buff[1] + offs[1]) = new_block(ino, inode, start_block + count);
				read_block(ext2->gdt[group].block_bitmap, block_bmp);
			} else if (start_block == current_block || !offs[0])
				read_block(*(buff[1] + offs[1]), buff[0]);

			*(buff[0] + offs[0]) = bno++;
		}

		pdepth = depth;
		current_block++;
	}

	if (block_off(end_block, offs) == 2)
		write_block(inode->block[offs[1]], buff[0]);
	else if (block_off(end_block, offs) == 3)
		write_block(inode->block[offs[2]], buff[1]);
	else if (block_off(end_block, offs) == 4)
		write_block(inode->block[offs[3]], buff[2]);

	write_block(ext2->gdt[group].block_bitmap, block_bmp);
	free(block_bmp);
	return count;
}

int set_blocks(u32 ino, ext2_inode_t *inode, u32 start_block, u32 count, void *data)
{
	u32 *buff[3];
	u32 block = start_block;
	u32 checkpoint = start_block;
	u32 last, current;

	buff[0] = malloc(ext2->block_size);
	buff[1] = malloc(ext2->block_size);
	buff[2] = malloc(ext2->block_size);

	last = 0;

	while (block < start_block + count) {
		current = get_block_no(inode, block, buff);
		if (!current) {
			if (checkpoint != block) {
				write_blocks(get_block_no(inode, checkpoint, buff), block - checkpoint,
						data + ((checkpoint - start_block) * ext2->block_size));
			}

			checkpoint = block;
			block++;
			while(block < start_block + count && !get_block_no(inode, block, buff))
				block++;

			/* alloc and write blocks */
			while (checkpoint != block) {
				last = alloc_blocks(ino, inode, checkpoint, block - checkpoint, last, buff);
				if (!last)
					return -ENOSPC; //no free blocks on disk

				write_blocks(get_block_no(inode, checkpoint, buff), last,
						data + ((checkpoint - start_block) * ext2->block_size));
				checkpoint += last;
			}

			last = 0;
		} else if (current == last + 1 || last == 0) {
			last = current;
			block++;
		} else {
			if (checkpoint != block)
				write_blocks(get_block_no(inode, checkpoint, buff), block - checkpoint,
						data + ((checkpoint - start_block) * ext2->block_size));
			checkpoint = block;
			last = 0;
			block++;
		}
	}
	if (checkpoint != block) {
		write_blocks(get_block_no(inode, checkpoint, buff), block - checkpoint,
				data + ((checkpoint - start_block) * ext2->block_size));
	}

	free(buff[0]);
	free(buff[1]);
	free(buff[2]);
	return EOK;
}
