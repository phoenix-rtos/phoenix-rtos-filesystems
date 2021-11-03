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


struct {
	rbtree_t dev;
	handle_t mutex;
} dev_common;


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


dummyfs_object_t *dev_find(oid_t *oid, int create)
{
	dummyfs_dev_t find, *entry;
	dummyfs_object_t *o;

	if (oid == NULL)
		return NULL;

	memcpy(&find.dev, oid, sizeof(oid_t));

	mutexLock(dev_common.mutex);
	entry = lib_treeof(dummyfs_dev_t, linkage, lib_rbFind(&dev_common.dev, &find.linkage));

	if (!create || entry != NULL) {
		o = entry != NULL ? object_get(entry->id) : NULL;
		mutexUnlock(dev_common.mutex);
		return o;
	}

	if ((entry = malloc(sizeof(dummyfs_dev_t))) == NULL) {
		mutexUnlock(dev_common.mutex);
		return NULL;
	}

	if ((o = object_create()) == NULL) {
		mutexUnlock(dev_common.mutex);
		free(entry);
		return NULL;
	}

	memcpy(&entry->dev, oid, sizeof(oid_t));
	memcpy(&o->dev, oid, sizeof(oid_t));
	entry->id = o->oid.id;
	lib_rbInsert(&dev_common.dev, &entry->linkage);
	mutexUnlock(dev_common.mutex);
	return o;
}


int dev_destroy(oid_t *oid)
{
	dummyfs_dev_t find, *entry;

	memcpy(&find.dev, oid, sizeof(oid_t));

	mutexLock(dev_common.mutex);
	entry = lib_treeof(dummyfs_dev_t, linkage, lib_rbFind(&dev_common.dev, &find.linkage));

	if (entry == NULL) {
		mutexUnlock(dev_common.mutex);
		return -EINVAL;
	}

	lib_rbRemove(&dev_common.dev, &entry->linkage);
	free(entry);
	mutexUnlock(dev_common.mutex);
	return EOK;
}


void dev_init()
{
	mutexCreate(&dev_common.mutex);
	lib_rbInit(&dev_common.dev, dev_cmp, NULL);
}
