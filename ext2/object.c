/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * object.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <sys/rb.h>
#include <sys/threads.h>

#include "ext2.h"
#include "inode.h"

#define MAX_FILES 512
#define CACHE_SIZE 128

struct {
	int i;
	handle_t ulock;
	handle_t clock;
	rbtree_t used;
	u32	used_cnt;
	ext2_object_t *cache[CACHE_SIZE];
} ext2_objects;

static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	ext2_object_t *o1 = lib_treeof(ext2_object_t, node, n1);
	ext2_object_t *o2 = lib_treeof(ext2_object_t, node, n2);

	/* possible overflow */
	return (o1->oid.id - o2->oid.id);
}


int object_detroy(ext2_object_t *o)
{
	mutexLock(ext2_objects.ulock);

	if (o->refs > 1) {
		mutexUnlock(ext2_objects.ulock);
		return -EBUSY;
	}
	lib_rbRemove(&ext2_objects.used, &o->node);

	if (inode_free(o->oid.id, o->inode) == EOK)
		free(o);

	mutexUnlock(ext2_objects.ulock);
	return EOK;
}

int object_remove(ext2_object_t *o)
{
	ext2_object_t *r;

	mutexLock(ext2_objects.ulock);
	if (o->refs > 0) {
		mutexUnlock(ext2_objects.ulock);
		return -EBUSY;
	}

	lib_rbRemove(&ext2_objects.used, &o->node);

	mutexLock(ext2_objects.clock);
	r  = ext2_objects.cache[o->oid.id % CACHE_SIZE];
	if (r != NULL && r != o) {
		if (r->dirty)
			inode_set(r->oid.id, r->inode);
		inode_put(r->inode);
		free(r);
	}
	ext2_objects.cache[o->oid.id % CACHE_SIZE] = o;
	mutexUnlock(ext2_objects.clock);

	mutexUnlock(ext2_objects.ulock);

	return EOK;
}

ext2_object_t *object_create(id_t id, ext2_inode_t *inode)
{
	ext2_object_t *o, t;

	t.oid.id = id;

	mutexLock(ext2_objects.ulock);

	if (ext2_objects.used_cnt >= MAX_FILES) {
		mutexUnlock(ext2_objects.ulock);
		return NULL;
	}

	if ((o = lib_treeof(ext2_object_t, node, lib_rbFind(&ext2_objects.used, &t.node))) != NULL) {
		o->refs++;
		mutexUnlock(ext2_objects.ulock);
		return o;
	}

	o = (ext2_object_t *)malloc(sizeof(ext2_object_t));
	if (o == NULL) {
		mutexUnlock(ext2_objects.ulock);
		return NULL;
	}

	memset(o, 0, sizeof(ext2_object_t));
	o->refs = 1;
	o->oid.id = id;
	o->inode = inode;

	o->oid.port = ext2->port;

	lib_rbInsert(&ext2_objects.used, &o->node);
	ext2_objects.used_cnt++;

	mutexUnlock(ext2_objects.ulock);

	return o;
}

ext2_object_t *object_get(unsigned int id)
{
	ext2_object_t *o, t;
	ext2_inode_t *inode;

	t.oid.id = id;

	mutexLock(ext2_objects.ulock);
	/* check used/opened inodes tree */
	if ((o = lib_treeof(ext2_object_t, node, lib_rbFind(&ext2_objects.used, &t.node))) != NULL) {
		o->refs++;
		mutexUnlock(ext2_objects.ulock);
		return o;
	} else {
		/* check recently closed cache */
		mutexLock(ext2_objects.clock);
		if ((o = ext2_objects.cache[id % CACHE_SIZE]) != NULL && o->oid.id == id) {
			o->refs++;
			lib_rbInsert(&ext2_objects.used, &o->node);
			mutexUnlock(ext2_objects.clock);
			mutexUnlock(ext2_objects.ulock);
			return o;
		}
		mutexUnlock(ext2_objects.clock);
	}
	inode = inode_get(id);

	if (inode != NULL) {

		mutexUnlock(ext2_objects.ulock);
		o = object_create(id, inode);

		return o;
	}
	mutexUnlock(ext2_objects.ulock);

	return o;
}


void object_put(ext2_object_t *o)
{
	mutexLock(ext2_objects.ulock);
	if (o != NULL && o->refs)
		o->refs--;

	if(!o->refs) {
		mutexUnlock(ext2_objects.ulock);
		object_remove(o);
		return;
	}
	mutexUnlock(ext2_objects.ulock);

	return;
}

void object_init(void)
{
	lib_rbInit(&ext2_objects.used, object_cmp, NULL);

	ext2_objects.used_cnt = 0;
	memset(&ext2_objects.cache, 0, CACHE_SIZE);

	mutexCreate(&ext2_objects.ulock);
	mutexCreate(&ext2_objects.clock);
}
