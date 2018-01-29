/*
 * Phoenix-RTOS
 *
 * Operating system kernel
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
#include <sys/threads.h>
#include <sys/msg.h>
#include <sys/rb.h>
#include <unistd.h>

#include "dummyfs.h"

rbtree_t file_objects = { 0 };
handle_t olock;

static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	dummyfs_object_t *o1 = lib_treeof(dummyfs_object_t, node, n1);
	dummyfs_object_t *o2 = lib_treeof(dummyfs_object_t, node, n2);

	/* possible overflow */
	return (o1->oid.id - o2->oid.id);
}


static int object_gapcmp(rbnode_t *n1, rbnode_t *n2)
{
	dummyfs_object_t *o1 = lib_treeof(dummyfs_object_t, node, n1);
	dummyfs_object_t *o2 = lib_treeof(dummyfs_object_t, node, n2);
	rbnode_t *child = NULL;
	int ret = 1;

	if (o1->lmaxgap > 0 && o1->rmaxgap > 0) {
		if (o2->oid.id > o1->oid.id) {
			child = n1->right;
			ret = -1;
		}
		else {
			child = n1->left;
			ret = 1;
		}
	}
	else if (o1->lmaxgap > 0) {
		child = n1->left;
		ret = 1;
	}
	else if (o1->rmaxgap > 0) {
		child = n1->right;
		ret = -1;
	}

	if (child == NULL)
		return 0;

	return ret;
}


static void object_augment(rbnode_t *node)
{
	rbnode_t *it;
	rbnode_t *parent = node->parent;
	dummyfs_object_t *n = lib_treeof(dummyfs_object_t, node, node);
	dummyfs_object_t *p = lib_treeof(dummyfs_object_t, node, parent);
	dummyfs_object_t *pp = (parent != NULL) ? lib_treeof(dummyfs_object_t, node, parent->parent) : NULL;
	dummyfs_object_t *l, *r;

	if (node->left == NULL) {
		if (parent != NULL && parent->right == node)
			n->lmaxgap = n->oid.id - p->oid.id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->right == parent)
			n->lmaxgap = n->oid.id - pp->oid.id - 1;
		else
			n->lmaxgap = n->oid.id;
	}
	else {
		l = lib_treeof(dummyfs_object_t, node, node->left);
		n->lmaxgap = max(l->lmaxgap, l->rmaxgap);
	}

	if (node->right == NULL) {
		if (parent != NULL && parent->left == node)
			n->rmaxgap = p->oid.id - n->oid.id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->left == parent)
			n->rmaxgap = pp->oid.id - n->oid.id - 1;
		else
			n->rmaxgap = (unsigned int)-1 - n->oid.id - 1;
	}
	else {
		r = lib_treeof(dummyfs_object_t, node, node->right);
		n->rmaxgap = max(r->lmaxgap, r->rmaxgap);
	}

	for (it = node; it->parent != NULL; it = it->parent) {
		n = lib_treeof(dummyfs_object_t, node, it);
		p = lib_treeof(dummyfs_object_t, node, it->parent);

		if (it->parent->left == it)
			p->lmaxgap = max(n->lmaxgap, n->rmaxgap);
		else
			p->rmaxgap = max(n->lmaxgap, n->rmaxgap);
	}
}


dummyfs_object_t *object_create(void)
{
	mutexLock(olock);
	dummyfs_object_t *r, t;
	u32 id;

	if (file_objects.root == NULL)
		id = 0;
	else {
		t.oid.id = 0;
		r = lib_treeof(dummyfs_object_t, node, lib_rbFindEx(file_objects.root, &t.node, object_gapcmp));
		if (r != NULL) {
			if (r->lmaxgap > 0)
				id = r->oid.id - 1;
			else
				id = r->oid.id + 1;
		}
		else {
			return NULL;
		}
	}

	mutexLock(dummyfs_common.mutex);
	if (dummyfs_incsz(sizeof(dummyfs_object_t)) != EOK)
		return NULL;

	r = (dummyfs_object_t *)malloc(sizeof(dummyfs_object_t));
	if (r == NULL) {
		dummyfs_decsz(sizeof(dummyfs_object_t));
		mutexUnlock(dummyfs_common.mutex);
		return NULL;
	}
	mutexUnlock(dummyfs_common.mutex);

	memset(r, 0, sizeof(dummyfs_object_t));
	r->oid.id = id;
	r->refs = 1;
	r->type = otUnknown;

	lib_rbInsert(&file_objects, &r->node);
	mutexUnlock(olock);

	return r;
}


int object_remove(dummyfs_object_t *o)
{
	mutexLock(olock);
	if (o->refs > 1)
		return -EBUSY;

	lib_rbRemove(&file_objects, &o->node);
	mutexUnlock(olock);

	return EOK;
}


dummyfs_object_t *object_get(unsigned int id)
{
	dummyfs_object_t *o, t;

	t.oid.id = id;

	mutexLock(olock);
	if ((o = lib_treeof(dummyfs_object_t, node, lib_rbFind(&file_objects, &t.node))) != NULL)
		o->refs++;
	mutexUnlock(olock);

	return o;
}


void object_put(dummyfs_object_t *o)
{
	mutexLock(olock);
	if (o != NULL && o->refs)
		o->refs--;
	mutexUnlock(olock);

	return;
}

void object_lock(dummyfs_object_t *o)
{
	mutexLock(dummyfs_common.mutex);
}

void object_unlock(dummyfs_object_t *o)
{
	mutexUnlock(dummyfs_common.mutex);
}

void object_init(void)
{
	lib_rbInit(&file_objects, object_cmp, object_augment);
	mutexCreate(&olock);
}
