/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Filesystem object
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


/* Object flags */
enum {
	OFLAG_DIRTY      = 0x01,
	OFLAG_MOUNTPOINT = 0x02,
	OFLAG_MOUNT      = 0x04
};


struct _ext2_obj_t {
	id_t id;                 /* Object ID, same as underlying inode number */
	rbnode_t node;           /* RBTree node */

	/* Object data */
	union {
		struct {
			uint32_t bno;
			uint32_t *data;
		} ind[3];            /* Indirect blocks */
		oid_t mnt;           /* Mounted filesystem */
		oid_t dev;           /* Device */
	};
	uint32_t refs;           /* Reference counter */
	uint8_t flags;           /* Object flags */
	ext2_inode_t *inode;     /* Underlying inode */
	ext2_obj_t *prev, *next; /* Double linked list */

	/* Synchronization */
	handle_t lock;           /* Access mutex */
};


struct _ext2_objs_t {
	rbtree_t used;           /* RBTree of objects in use */
	uint32_t count;          /* Number of objects in use */
	ext2_obj_t *lru;         /* Least Recently Used objects cache */

	/* Synchronization */
	handle_t lock;           /* Access mutex */
};


/* Creates new object */
extern int ext2_obj_create(ext2_t *fs, uint32_t pino, ext2_inode_t *inode, uint16_t mode, ext2_obj_t **res);


/* Destroys object */
extern int ext2_obj_destroy(ext2_t *fs, ext2_obj_t *obj);


/* Retrives object */
extern ext2_obj_t *ext2_obj_get(ext2_t *fs, id_t id);


/* Synchronizes object (requires object to be locked) */
extern int _ext2_obj_sync(ext2_t *fs, ext2_obj_t *obj);


/* Synchronizes object */
extern int ext2_obj_sync(ext2_t *fs, ext2_obj_t *obj);


/* Releases object */
extern void ext2_obj_put(ext2_t *fs, ext2_obj_t *obj);


/* Destroys filesystem objects */
extern void ext2_objs_destroy(ext2_t *fs);


/* Initializes filesystem objects */
extern int ext2_objs_init(ext2_t *fs);


#endif
