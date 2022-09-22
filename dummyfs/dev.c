/*
 * Phoenix-RTOS
 *
 * dummyfs - devices
 *
 * Copyright 2018 Phoenix Systems
 * Author: Jan Sikorski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <sys/rb.h>
#include <sys/threads.h>
#include <string.h>

#include "dummyfs_internal.h"
#include "object.h"
#include "dev.h"


typedef struct {
	rbnode_t linkage;
	id_t id;
	oid_t dev;
} dummyfs_dev_t;


static int dev_cmp(rbnode_t *n1, rbnode_t *n2)
{
	dummyfs_dev_t *d1 = lib_treeof(dummyfs_dev_t, linkage, n1);
	dummyfs_dev_t *d2 = lib_treeof(dummyfs_dev_t, linkage, n2);

	if (d1->dev.port != d2->dev.port)
		return d1->dev.port > d2->dev.port ? 1 : -1;

	if (d1->dev.id != d2->dev.id)
		return d1->dev.id > d2->dev.id ? 1 : -1;

	return 0;
}


dummyfs_object_t *dev_find(dummyfs_t *ctx, oid_t *oid)
{
	dummyfs_dev_t find, *entry;
	dummyfs_object_t *o;

	if (oid == NULL) {
		return NULL;
	}

	memcpy(&find.dev, oid, sizeof(oid_t));

	mutexLock(ctx->devlock);
	entry = lib_treeof(dummyfs_dev_t, linkage, lib_rbFind(&ctx->devtree, &find.linkage));
	o = entry != NULL ? object_get(ctx, entry->id) : NULL;
	mutexUnlock(ctx->devlock);

	return o;
}

int dev_create(dummyfs_t *ctx, oid_t *oid, dummyfs_object_t *obj)
{
	dummyfs_dev_t *entry;

	if (oid == NULL || obj == NULL) {
		return -EINVAL;
	}

	entry = malloc(sizeof(dummyfs_dev_t));
	if (entry == NULL) {
		return -ENOMEM;
	}

	/* Update object and device parameters, so they point to each other */
	mutexLock(ctx->devlock);
	object_lock(ctx, obj);
	entry->id = obj->oid.id;
	entry->dev = *oid;
	obj->dev = *oid;
	lib_rbInsert(&ctx->devtree, &entry->linkage);

	object_unlock(ctx, obj);
	mutexUnlock(ctx->devlock);
	return EOK;
}


int dev_destroy(dummyfs_t *ctx, oid_t *oid)
{
	dummyfs_dev_t find, *entry;

	memcpy(&find.dev, oid, sizeof(oid_t));

	mutexLock(ctx->devlock);
	entry = lib_treeof(dummyfs_dev_t, linkage, lib_rbFind(&ctx->devtree, &find.linkage));

	if (entry == NULL) {
		mutexUnlock(ctx->devlock);
		return -EINVAL;
	}

	lib_rbRemove(&ctx->devtree, &entry->linkage);
	free(entry);
	mutexUnlock(ctx->devlock);
	return EOK;
}


void dev_cleanup(dummyfs_t *ctx)
{
	dummyfs_dev_t *entry;
	rbnode_t *n;

	mutexLock(ctx->devlock);
	while ((n = lib_rbMinimum(ctx->devtree.root)) != NULL) {
		entry = lib_treeof(dummyfs_dev_t, linkage, n);
		lib_rbRemove(&ctx->devtree, &entry->linkage);
		free(entry);
	}
	mutexUnlock(ctx->devlock);

	resourceDestroy(ctx->devlock);
}


int dev_init(dummyfs_t *ctx)
{
	if (mutexCreate(&ctx->devlock) != EOK)
		return -ENOMEM;

	lib_rbInit(&ctx->devtree, dev_cmp, NULL);

	return EOK;
}
