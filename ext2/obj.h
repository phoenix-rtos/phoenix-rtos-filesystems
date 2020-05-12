/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Object
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _OBJ_H_
#define _OBJ_H_


#include <stdint.h>

#include <sys/types.h>
#include <sys/rb.h>

#include "ext2.h"
#include "inode.h"


/* ext2 object flags */
enum {
	EXT2_FL_DIRTY = 1,
	EXT2_FL_MOUNTPOINT = 2,
	EXT2_FL_MOUNT = 4
};


typedef struct ext2_obj_t ext2_obj_t;

struct ext2_obj_t {
	id_t ino;
	rbnode_t node;

	uint32_t refs;
	uint8_t flags;
	ext2_inode_t *inode;
	union {
		struct {
			uint32_t block;
			uint32_t *data;
		} ind[3];
		oid_t mnt;
	};

	handle_t lock;
	ext2_obj_t *prev, *next;
};


typedef struct {
	rbtree_t used;   /* Objects in use */
	uint32_t count;  /* Number of objects in use */
	ext2_obj_t *lru; /* Least Recently Used objects cache */

	/* Synchronization */
	handle_t lock;   /* Objects mutex */
} ext2_objs_t;


extern int ext2_init_objs(ext2_t *fs);


extern ext2_obj_t *object_get(ext2_t *f, id_t *id);


extern void object_put(ext2_obj_t *o);


extern void object_sync(ext2_obj_t *o);


extern ext2_obj_t *object_create(ext2_t *f, id_t *id, id_t *pid, ext2_inode_t **inode, int mode);


extern int object_destroy(ext2_obj_t *o);


__attribute__((always_inline)) inline int object_checkFlag(ext2_obj_t *o, uint8_t flag)
{
	return o->flags & flag;
}


__attribute__((always_inline)) inline int object_setFlag(ext2_obj_t *o, uint8_t flag)
{
	return o->flags |= flag;
}


__attribute__((always_inline)) inline int object_clearFlag(ext2_obj_t *o, uint8_t flag)
{
	return o->flags &= ~flag;
}


#endif
