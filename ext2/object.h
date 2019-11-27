/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * object.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _OBJECT_H_
#define _OBJECT_H_ /* object.h */

#include <sys/types.h>


extern ext2_object_t *object_get(ext2_fs_info_t *f, id_t *id);


extern void object_put(ext2_object_t *o);


extern void object_sync(ext2_object_t *o);


extern ext2_object_t *object_create(ext2_fs_info_t *f, id_t *id, id_t *pid, ext2_inode_t **inode, int mode);


extern int object_destroy(ext2_object_t *o);


extern int object_init(ext2_fs_info_t *f);


__attribute__((always_inline)) inline int object_checkFlag(ext2_object_t *o, uint8_t flag)
{
	return o->flags & flag;
}


__attribute__((always_inline)) inline int object_setFlag(ext2_object_t *o, uint8_t flag)
{
	return o->flags |= flag;
}


__attribute__((always_inline)) inline int object_clearFlag(ext2_object_t *o, uint8_t flag)
{
	return o->flags &= ~flag;
}


#endif /* object.h */
