/*
 * Phoenix-RTOS
 *
 * dummyfs - object storage
 *
 * Copyright 2018 Phoenix Systems
 * Author: Pawel Pisarczyk, Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/threads.h>
#include <sys/msg.h>
#include <sys/rb.h>
#include <posix/idtree.h>
#include <unistd.h>

#include "dummyfs_internal.h"
#include "dir.h"

idtree_t dummytree = { 0 };
handle_t olock;

#define dummy_node2obj(n) lib_treeof(dummyfs_object_t, node, n)
extern int dummyfs_destroy(oid_t *oid);

dummyfs_object_t *object_create(void)
{
	mutexLock(olock);
	dummyfs_object_t *r;
	int id;

	mutexLock(dummyfs_common.mutex);
	r = (dummyfs_object_t *)malloc(sizeof(dummyfs_object_t));
	if (r == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		mutexUnlock(olock);
		return NULL;
	}

	memset(r, 0, sizeof(dummyfs_object_t));

	id = idtree_alloc(&dummytree, &r->node);

	if (id < 0) {
		free(r);
		mutexUnlock(dummyfs_common.mutex);
		mutexUnlock(olock);
		return NULL;
	}

	if (dummyfs_incsz(sizeof(dummyfs_object_t)) != EOK) {
		free(r);
		mutexUnlock(dummyfs_common.mutex);
		mutexUnlock(olock);
		return NULL;
	}

	mutexUnlock(dummyfs_common.mutex);

	r->oid.id = id;
	r->refs = 1;
	r->mode = 0;
	r->nlink = 0;

	mutexUnlock(olock);

	return r;
}


void object_lock(dummyfs_object_t *o)
{
	mutexLock(dummyfs_common.mutex);
}


void object_unlock(dummyfs_object_t *o)
{
	mutexUnlock(dummyfs_common.mutex);
}


int object_remove(dummyfs_object_t *o)
{
	if (o->nlink || o->refs)
		return -EBUSY;

	idtree_remove(&dummytree, &o->node);
	return EOK;
}


dummyfs_object_t *object_get_unlocked(unsigned int id)
{
	dummyfs_object_t *o;

	o = dummy_node2obj(idtree_find(&dummytree, id));

	return o;
}


dummyfs_object_t *object_get(unsigned int id)
{
	dummyfs_object_t *o;

	mutexLock(olock);
	if ((o = object_get_unlocked(id)) != NULL)
		o->refs++;
	mutexUnlock(olock);

	return o;
}


void object_put(dummyfs_object_t *o)
{
	mutexLock(olock);
	if (o != NULL && o->refs) {
		o->refs--;

		if (!o->refs && S_ISDIR(o->mode) && o->dirty) {
			object_lock(o);
			dir_clean(o);
			object_unlock(o);
		}

		if (!o->refs && !o->nlink)
			dummyfs_destroy(&o->oid);
	}
	mutexUnlock(olock);
	return;
}


void object_init(void)
{
	idtree_init(&dummytree);
	mutexCreate(&olock);
}
