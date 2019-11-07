/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * inode.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _INODE_H_
#define _INODE_H_ /* inode.h */

#include <stdint.h>

extern ext2_inode_t *inode_get(ext2_fs_info_t *f, uint32_t ino);


extern uint32_t inode_create(ext2_fs_info_t *f, ext2_inode_t *inode, uint32_t mode, uint32_t pino);


extern int inode_free(ext2_fs_info_t *f, uint32_t ino, ext2_inode_t *inode);


extern int inode_put(ext2_inode_t *inode);


extern int inode_set(ext2_fs_info_t *f, uint32_t ino, ext2_inode_t *inode);


#endif /* inode.h */
