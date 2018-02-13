/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * block.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _BLOCK_H_
#define _BLOCK_H_ /* block.h */

#include "ext2.h"

extern int write_block(u32 block, void *data);


extern int read_block(u32 block, void *data);


extern int read_blocks(u32 block, void *data, u32 size);


extern u32 search_block(void *data, const char *name, u8 len);


extern u32 get_block_no(ext2_inode_t *inode, u32 block, u32 *buff[3]);


extern int free_inode_block(ext2_inode_t *inode, u32 block, u32 *buff[3]);


extern u32 new_block(u32 ino, ext2_inode_t *inode, u32 bno);


extern void get_block(ext2_inode_t *inode, u32 block, void *data,
		u32 off[4], u32 prev_off[4], u32 *buff[3]);


extern void set_block(u32 ino, ext2_inode_t *inode, u32 block, void *data,
		u32 off[4], u32 prev_off[4], u32 *buff[3]);


static inline u32 block_offset(u32 block_no)
{
	return ext2->first_block + (ext2->block_size * (block_no - 1));
}

#endif /* block.h */
