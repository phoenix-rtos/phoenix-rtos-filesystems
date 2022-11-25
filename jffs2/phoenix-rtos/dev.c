/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */
#include <stdlib.h>
#include <sys/rb.h>
#include <sys/threads.h>
#include <string.h>

#include "../phoenix-rtos.h"
#include "dev.h"

int old_valid_dev(dev_t dev)
{
	return 0;
}

int old_encode_dev(dev_t dev)
{
	return dev;
}

int new_encode_dev(dev_t dev)
{
	return dev;
}

dev_t old_decode_dev(uint16_t dev)
{
	return dev;
}

dev_t new_decode_dev(uint32_t dev)
{
	return dev;
}

typedef struct _dev_common_t {
	rbtree_t dev_oid;
	rbtree_t dev_ino;
	handle_t mutex;
} dev_common_t;


static int dev_cmp_oid(rbnode_t *n1, rbnode_t *n2)
{
	jffs2_dev_t *d1 = lib_treeof(jffs2_dev_t, linkage_oid, n1);
	jffs2_dev_t *d2 = lib_treeof(jffs2_dev_t, linkage_oid, n2);

	if (d1->dev.port != d2->dev.port)
		return d1->dev.port > d2->dev.port ? 1 : -1;

	if (d1->dev.id != d2->dev.id)
		return d1->dev.id > d2->dev.id ? 1 : -1;

	return 0;
}


static int dev_cmp_ino(rbnode_t *n1, rbnode_t *n2)
{
	jffs2_dev_t *d1 = lib_treeof(jffs2_dev_t, linkage_ino, n1);
	jffs2_dev_t *d2 = lib_treeof(jffs2_dev_t, linkage_ino, n2);

	if (d1->ino != d2->ino)
		return d1->ino > d2->ino ? 1 : -1;

	return 0;
}


jffs2_dev_t *dev_find_oid(void *ptr, oid_t *oid, unsigned long ino, int create)
{
	jffs2_dev_t find, *entry;
	dev_common_t *dev_common = ptr;

	memcpy(&find.dev, oid, sizeof(oid_t));

	mutexLock(dev_common->mutex);

	entry = lib_treeof(jffs2_dev_t, linkage_oid, lib_rbFind(&dev_common->dev_oid, &find.linkage_oid));

	if (!create || entry != NULL) {
		mutexUnlock(dev_common->mutex);
		return entry;
	}

	if ((entry = malloc(sizeof(jffs2_dev_t))) == NULL) {
		mutexUnlock(dev_common->mutex);
		return NULL;
	}

	entry->ino = ino;
	memcpy(&entry->dev, oid, sizeof(oid_t));
	lib_rbInsert(&dev_common->dev_ino, &entry->linkage_ino);
	lib_rbInsert(&dev_common->dev_oid, &entry->linkage_oid);

	mutexUnlock(dev_common->mutex);

	return entry;
}


jffs2_dev_t *dev_find_ino(void *ptr, unsigned long ino)
{
	jffs2_dev_t find, *entry;
	dev_common_t *dev_common = ptr;

	find.ino = ino;

	mutexLock(dev_common->mutex);

	entry = lib_treeof(jffs2_dev_t, linkage_ino, lib_rbFind(&dev_common->dev_ino, &find.linkage_ino));

	mutexUnlock(dev_common->mutex);

	return entry;
}


static void _dev_destroy(dev_common_t *dev_common, jffs2_dev_t *dev)
{
	lib_rbRemove(&dev_common->dev_ino, &dev->linkage_ino);
	lib_rbRemove(&dev_common->dev_oid, &dev->linkage_oid);
	free(dev);
}


void dev_destroy(void *ptr, jffs2_dev_t *dev)
{
	dev_common_t *dev_common = ptr;

	if (dev != NULL) {
		mutexLock(dev_common->mutex);

		_dev_destroy(dev_common, dev);

		mutexUnlock(dev_common->mutex);
	}
}


void dev_done(void *ptr)
{
	dev_common_t *dev_common = (dev_common_t *)ptr;
	rbnode_t *node, *next;
	jffs2_dev_t *dev;

	node = lib_rbMinimum(dev_common->dev_ino.root);
	while (node != NULL) {
		dev = lib_treeof(jffs2_dev_t, linkage_ino, node);
		next = lib_rbNext(node);

		_dev_destroy(dev_common, dev);

		node = next;
	}

	resourceDestroy(dev_common->mutex);
	free(dev_common);
}


void dev_init(void **ptr)
{
	dev_common_t *dev_common = malloc(sizeof(dev_common_t));

	mutexCreate(&dev_common->mutex);
	lib_rbInit(&dev_common->dev_ino, dev_cmp_ino, NULL);
	lib_rbInit(&dev_common->dev_oid, dev_cmp_oid, NULL);

	*ptr = dev_common;
}
