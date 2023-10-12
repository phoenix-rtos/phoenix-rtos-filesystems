/*
 * Phoenix-RTOS
 *
 * dummyfs - object storage
 *
 * Copyright 2018, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Kamil Amanowicz, Aleksander Kaminski
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
#include <assert.h>

#include "dummyfs_internal.h"
#include "dummyfs.h"
#include "memory.h"
#include "dir.h"


#define dummy_node2obj(n) lib_treeof(dummyfs_object_t, node, n)


dummyfs_object_t *dummyfs_object_create(dummyfs_t *ctx)
{
	TRACE();
	dummyfs_object_t *r = dummyfs_calloc(ctx, sizeof(dummyfs_object_t));
	if (r == NULL) {
		return NULL;
	}

	int id = idtree_alloc(&ctx->dummytree, &r->node);
	if (id < 0) {
		dummyfs_free(ctx, r, sizeof(dummyfs_object_t));
		return NULL;
	}

	r->oid.id = id;
	r->refs = 1;

	return r;
}


int dummyfs_object_remove(dummyfs_t *ctx, dummyfs_object_t *o)
{
	TRACE();
	assert((o->nlink >= 0) && (o->refs >= 0));

	if ((o->nlink > 0) || (o->refs > 0)) {
		return -EBUSY;
	}

	if (S_ISDIR(o->mode) && (dummyfs_dir_empty(ctx, o) < 0)) {
		return -EBUSY;
	}

	idtree_remove(&ctx->dummytree, &o->node);
	return 0;
}


dummyfs_object_t *dummyfs_object_find(dummyfs_t *ctx, oid_t *oid)
{
	TRACE();
	dummyfs_object_t *o = dummy_node2obj(idtree_find(&ctx->dummytree, oid->id));

	return o;
}


dummyfs_object_t *dummyfs_object_get(dummyfs_t *ctx, oid_t *oid)
{
	TRACE();
	dummyfs_object_t *o = dummyfs_object_find(ctx, oid);
	if (o != NULL) {
		o->refs++;
	}

	return o;
}


void dummyfs_object_put(dummyfs_t *ctx, dummyfs_object_t *o)
{
	TRACE();
	assert((o != NULL) && (o->refs > 0));

	o->refs--;
	if ((o->refs == 0) && (o->nlink == 0)) {
		_dummyfs_destroy(ctx, &o->oid);
	}
}


int dummyfs_object_init(dummyfs_t *ctx)
{
	TRACE();
	idtree_init(&ctx->dummytree);

	return 0;
}


void dummyfs_object_cleanup(dummyfs_t *ctx)
{
	TRACE();
	/* Forcibly destroy all objects, ignore refs and links */
	for (;;) {
		rbnode_t *n = lib_rbMinimum(ctx->dummytree.root);
		if (n == NULL) {
			break;
		}
		dummyfs_object_t *o = dummy_node2obj(n);
		idtree_remove(&ctx->dummytree, &o->node);
		_dummyfs_destroy(ctx, &o->oid);
	}
}
