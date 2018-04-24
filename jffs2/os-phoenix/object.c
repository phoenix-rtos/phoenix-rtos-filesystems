/*
 * Phoenix-RTOS
 *
 * jffs2
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
#include <errno.h>
#include <string.h>
#include <sys/rb.h>
#include <sys/threads.h>

#include "object.h"

struct {
	handle_t	lock;
	rbtree_t	tree;
	u32			cnt;
} jffs2_objects;


static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	jffs2_object_t *o1 = lib_treeof(jffs2_object_t, node, n1);
	jffs2_object_t *o2 = lib_treeof(jffs2_object_t, node, n2);

	/* possible overflow */
	return (o1->oid.id - o2->oid.id);
}

static int object_gapcmp(rbnode_t *n1, rbnode_t *n2)
{
	jffs2_object_t *o1 = lib_treeof(jffs2_object_t, node, n1);
	jffs2_object_t *o2 = lib_treeof(jffs2_object_t, node, n2);
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
	jffs2_object_t *n = lib_treeof(jffs2_object_t, node, node);
	jffs2_object_t *p = lib_treeof(jffs2_object_t, node, parent);
	jffs2_object_t *pp = (parent != NULL) ? lib_treeof(jffs2_object_t, node, parent->parent) : NULL;
	jffs2_object_t *l, *r;

	if (node->left == NULL) {
		if (parent != NULL && parent->right == node)
			n->lmaxgap = n->oid.id - p->oid.id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->right == parent)
			n->lmaxgap = n->oid.id - pp->oid.id - 1;
		else
			n->lmaxgap = n->oid.id;
	}
	else {
		l = lib_treeof(jffs2_object_t, node, node->left);
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
		r = lib_treeof(jffs2_object_t, node, node->right);
		n->rmaxgap = max(r->lmaxgap, r->rmaxgap);
	}

	for (it = node; it->parent != NULL; it = it->parent) {
		n = lib_treeof(jffs2_object_t, node, it);
		p = lib_treeof(jffs2_object_t, node, it->parent);

		if (it->parent->left == it)
			p->lmaxgap = max(n->lmaxgap, n->rmaxgap);
		else
			p->rmaxgap = max(n->lmaxgap, n->rmaxgap);
	}
}

void object_destroy(jffs2_object_t *o)
{
	if (o->state == ST_LOCKED)
		return;
}


int object_remove(jffs2_object_t *o)
{
	if (o->state == ST_LOCKED)
		return EOK;

	mutexLock(jffs2_objects.lock);

	lib_rbRemove(&jffs2_objects.tree, &o->node);

	mutexUnlock(jffs2_objects.lock);

	return EOK;
}

jffs2_object_t *object_create(int type)
{
	jffs2_object_t *r, t;
	u32 id;

	mutexLock(jffs2_objects.lock);
	t.oid.id = 0;
	r = lib_treeof(jffs2_object_t, node, lib_rbFindEx(jffs2_objects.tree.root, &t.node, object_gapcmp));
	if (r != NULL) {
		if (r->lmaxgap > 0)
			id = r->oid.id - 1;
		else
			id = r->oid.id + 1;
	} else {
		mutexUnlock(jffs2_objects.lock);
		return NULL;
	}

	memset(r, 0, sizeof(jffs2_object_t));
	r->oid.id = id;
	r->refs = 1;
	r->type = type;
	r->state = ST_LOCKED;
	r->inode_info = NULL;

	lib_rbInsert(&jffs2_objects.tree, &r->node);
	mutexUnlock(jffs2_objects.lock);

	return r;
}

jffs2_object_t *object_get(unsigned int id)
{
	jffs2_object_t *o, t;

	t.oid.id = id;

	mutexLock(jffs2_objects.lock);
	if ((o = lib_treeof(jffs2_object_t, node, lib_rbFind(&jffs2_objects.tree, &t.node))) != NULL)
		o->refs++;
	mutexUnlock(jffs2_objects.lock);

	return o;
}

void object_put(jffs2_object_t *o)
{
	if (o->state == ST_LOCKED)
		return;
}

void object_init(void)
{
	lib_rbInit(&jffs2_objects.tree, object_cmp, object_augment);
	mutexCreate(&jffs2_objects.lock);
}
