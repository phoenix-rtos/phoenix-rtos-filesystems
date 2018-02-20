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


extern int read_blocks(u32 block, u32 count, void *data);


extern u32 search_block(void *data, const char *name, u8 len);


extern void free_blocks(u32 start, u32 count);


extern int free_inode_blocks(ext2_object_t *o, u32 block, u32 count);


extern void get_block(ext2_object_t *o, u32 block, void *data);


extern u32 get_block_no(ext2_object_t *o, u32 block);


extern void set_block(ext2_object_t *o, u32 block, void *data);


extern int set_blocks(ext2_object_t *o, u32 start_block, u32 count, void *data);


static inline u32 block_offset(u32 block_no)
{
	return ext2->first_block + (ext2->block_size * (block_no - 1));
}

#endif /* block.h */
