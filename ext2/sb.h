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

extern void gdt_sync(ext2_fs_info_t *f, int group);


extern int ext2_read_sb(id_t *devId, ext2_fs_info_t *f);


extern int ext2_write_sb(ext2_fs_info_t *f);


extern int ext2_mount(id_t *devId, void **fsData);

#endif /* sb.h */
