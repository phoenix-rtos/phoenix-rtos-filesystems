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

#ifndef _BLOCK_H_
#define _BLOCK_H_


#include <stdint.h>

#include "ext2.h"
#include "obj.h"


extern int ext2_read_blocks(ext2_t *info, uint32_t start, uint32_t blocks, void *data);


extern int ext2_write_blocks(ext2_t *info, uint32_t start, uint32_t blocks, const void *data);


extern int ext2_get_block(ext2_t *fs, ext2_obj_t *obj, uint32_t block, uint32_t *res);


extern int ext2_init_block(ext2_t *fs, ext2_obj_t *obj, uint32_t block, void *data);


extern int ext2_sync_block(ext2_t *fs, ext2_obj_t *obj, uint32_t block, const void *data);


extern int ext2_sync_blocks(ext2_t *fs, ext2_obj_t *obj, uint32_t start, uint32_t blocks, const void *data);


extern int ext2_destroy_blocks(ext2_t *fs, uint32_t start, uint32_t blocks);


extern int ext2_destroy_iblocks(ext2_t *fs, ext2_obj_t *obj, uint32_t start, uint32_t blocks);


#endif
