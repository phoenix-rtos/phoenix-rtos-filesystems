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


extern ext2_inode_t *inode_get(u32 ino);


extern u32 inode_create(ext2_inode_t *inode, u32 mode);


extern int inode_free(u32 ino, ext2_inode_t *inode);


extern int inode_put(ext2_inode_t *inode);


extern int inode_set(u32 ino, ext2_inode_t *inode);


#endif /* inode.h */
