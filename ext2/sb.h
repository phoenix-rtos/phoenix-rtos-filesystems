/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * sb.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _SB_H_
#define _SB_H_ /* sb.h */

#define SUPERBLOCK_SIZE 1024


extern void gdt_sync(int group);


extern int ext2_read_sb(uint32_t sect);


extern int ext2_write_sb(void);


extern int ext2_init(void);

#endif /* sb.h */
