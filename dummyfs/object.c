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
#include "dummyfs.h"
#include "dir.h"


#define dummy_node2obj(n) lib_treeof(dummyfs_object_t, node, n)

dummyfs_object_t *object_create(dummyfs_t *ctx)
{
	mutexLock(ctx->olock);
	dummyfs_object_t *r;
	int id;

	mutexLock(ctx->mutex);
	r = (dummyfs_object_t *)malloc(sizeof(dummyfs_object_t));
	if (r == NULL) {
		mutexUnlock(ctx->mutex);
		mutexUnlock(ctx->olock);
		return NULL;
	}

	memset(r, 0, sizeof(dummyfs_object_t));

	id = idtree_alloc(&ctx->dummytree, &r->node);

	if (id < 0) {
		free(r);
		mutexUnlock(ctx->mutex);
		mutexUnlock(ctx->olock);
		return NULL;
	}

	if (dummyfs_incsz(ctx, sizeof(dummyfs_object_t)) != EOK) {
		free(r);
		mutexUnlock(ctx->mutex);
		mutexUnlock(ctx->olock);
		return NULL;
	}

	mutexUnlock(ctx->mutex);

	r->oid.id = id;
	r->refs = 1;
	r->mode = 0;
	r->nlink = 0;

	mutexUnlock(ctx->olock);

	return r;
}


void object_lock(dummyfs_t *ctx, dummyfs_object_t *o)
{
	mutexLock(ctx->mutex);
}


void object_unlock(dummyfs_t *ctx, dummyfs_object_t *o)
{
	mutexUnlock(ctx->mutex);
}


int object_remove(dummyfs_t *ctx, dummyfs_object_t *o)
{
	if (o->nlink || o->refs)
		return -EBUSY;

	idtree_remove(&ctx->dummytree, &o->node);
	return EOK;
}


dummyfs_object_t *object_get_unlocked(dummyfs_t *ctx, unsigned int id)
{
	dummyfs_object_t *o;

	o = dummy_node2obj(idtree_find(&ctx->dummytree, id));

	return o;
}


dummyfs_object_t *object_get(dummyfs_t *ctx, unsigned int id)
{
	dummyfs_object_t *o;

	mutexLock(ctx->olock);
	if ((o = object_get_unlocked(ctx, id)) != NULL)
		o->refs++;
	mutexUnlock(ctx->olock);

	return o;
}


void object_put(dummyfs_t *ctx, dummyfs_object_t *o)
{
	mutexLock(ctx->olock);
	if (o != NULL && o->refs) {
		o->refs--;

		if (!o->refs && S_ISDIR(o->mode) && o->dirty) {
			object_lock(ctx, o);
			dir_clean(ctx, o);
			object_unlock(ctx, o);
		}

		if (!o->refs && !o->nlink)
			dummyfs_destroy(ctx, &o->oid);
	}
	mutexUnlock(ctx->olock);
	return;
}


int object_init(dummyfs_t *ctx)
{
	if (mutexCreate(&ctx->olock) != EOK)
		return -ENOMEM;

	idtree_init(&ctx->dummytree);

	return EOK;
}


void object_cleanup(dummyfs_t *ctx)
{
	dummyfs_object_t *o;
	rbnode_t *n;

	mutexLock(ctx->olock);
	/* Forcibly destroy all objects, ignore refs and links */
	while ((n = lib_rbMinimum(ctx->dummytree.root)) != NULL) {
		o = dummy_node2obj(n);
		if (S_ISDIR(o->mode))
			dir_clean(ctx, o);
		idtree_remove(&ctx->dummytree, &o->node);
		dummyfs_destroy(ctx, &o->oid);
	}
	mutexUnlock(ctx->olock);

	resourceDestroy(ctx->devlock);
}
