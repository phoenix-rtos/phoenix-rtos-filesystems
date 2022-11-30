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

	struct list_head list;
	rbnode_t node;
} jffs2_object_t;


int object_insert(void *part, struct inode *inode);


jffs2_object_t *object_get(void *part, unsigned int id, int create);


void object_put(void *part, unsigned int id);


void object_done(void *part);


void object_init(void *part);


#endif /* _OS_PHOENIX_OBJECT_H_ */
