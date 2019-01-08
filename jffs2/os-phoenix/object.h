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

#ifndef _OS_PHOENIX_OBJECT_H_
#define _OS_PHOENIX_OBJECT_H_ /* object.h */


typedef struct _jffs2_object_t {
	oid_t oid;
	struct inode *inode;

	u32 refs;

	struct list_head list;
	rbnode_t node;
	handle_t lock;
} jffs2_object_t;


void object_init(void *part);


int object_insert(void *part, struct inode *inode);


jffs2_object_t *object_get(void *part, unsigned int id, int create);


void object_destroy(void *part, jffs2_object_t *o);


void object_put(void *part, unsigned int id);


#endif /* _OS_PHOENIX_OBJECT_H_ */
