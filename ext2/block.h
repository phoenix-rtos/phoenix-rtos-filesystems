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

#include <stdint.h>
#include "ext2.h"

extern int write_block(ext2_fs_info_t *f, uint32_t block, const void *data);


extern int read_block(ext2_fs_info_t *f, uint32_t block, void *data);


extern int read_blocks(ext2_fs_info_t *f, uint32_t block, uint32_t count, void *data);


extern int search_block(ext2_fs_info_t *f, void *data, const char *name, uint8_t len, id_t *resId);


extern void free_blocks(ext2_fs_info_t *f, uint32_t start, uint32_t count);


extern int free_inode_blocks(ext2_object_t *o, uint32_t block, uint32_t count);


extern void get_block(ext2_object_t *o, uint32_t block, void *data);


extern uint32_t get_block_no(ext2_object_t *o, uint32_t block);


extern void set_block(ext2_object_t *o, uint32_t block, const void *data);


extern int set_blocks(ext2_object_t *o, uint32_t start_block, uint32_t count, const void *data);


static inline uint32_t block_offset(ext2_fs_info_t *f, uint32_t block_no)
{
	return f->block_size * block_no;
}

#endif /* block.h */
