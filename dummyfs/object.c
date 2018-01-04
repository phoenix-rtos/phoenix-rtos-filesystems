/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs - object storage
 *
 * Copyright 2018 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/threads.h>
#include <sys/msg.h>
#include <sys/rb.h>
#include <unistd.h>

#include "dummyfs.h"


static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	dummyfs_object_t *o1 = lib_treeof(dummyfs_object_t, linkage, n1);
	dummyfs_object_t *o2 = lib_treeof(dummyfs_object_t, linkage, n2);

	return (o1->id - o2->id);
}


static int object_gapcmp(rbnode_t *n1, rbnode_t *n2)
{
	dummyfs_object_t *o1 = lib_treeof(dummyfs_object_t, linkage, n1);
	dummyfs_object_t *o2 = lib_treeof(dummyfs_object_t, linkage, n2);
	rbnode_t *child = NULL;
	int ret = 1;

	if (o1->lmaxgap > 0 && o1->rmaxgap > 0) {
		if (o2->id > o1->id) {
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
	dummyfs_object_t *n = lib_treeof(dummyfs_object_t, linkage, node);
	dummyfs_object_t *p = lib_treeof(dummyfs_object_t, linkage, parent);
	dummyfs_object_t *pp = (parent != NULL) ? lib_treeof(dummyfs_object_t, linkage, parent->parent) : NULL;
	dummyfs_object_t *l, *r;

	if (node->left == NULL) {
		if (parent != NULL && parent->right == node)
			n->lmaxgap = n->id - p->id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->right == parent)
			n->lmaxgap = n->id - pp->id - 1;
		else
			n->lmaxgap = n->id;
	}
	else {
		l = lib_treeof(dummyfs_object_t, linkage, node->left);
		n->lmaxgap = max(l->lmaxgap, l->rmaxgap);
	}

	if (node->right == NULL) {
		if (parent != NULL && parent->left == node)
			n->rmaxgap = p->id - n->id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->left == parent)
			n->rmaxgap = pp->id - n->id - 1;
		else
			n->rmaxgap = (unsigned int)-1 - n->id - 1;
	}
	else {
		r = lib_treeof(dummyfs_object_t, linkage, node->right);
		n->rmaxgap = max(r->lmaxgap, r->rmaxgap);
	}

	for (it = node; it->parent != NULL; it = it->parent) {
		n = lib_treeof(dummyfs_object_t, linkage, it);
		p = lib_treeof(dummyfs_object_t, linkage, it->parent);

		if (it->parent->left == it)
			p->lmaxgap = max(n->lmaxgap, n->rmaxgap);
		else
			p->rmaxgap = max(n->lmaxgap, n->rmaxgap);
	}
}


dummyfs_object_t *object_create(dummyfs_object_t *objects, unsigned int *id)
{
	dummyfs_object_t *r, t;

	proc_lockSet(&process->lock);

	if (process->resources.root == NULL)
		*id = 0;
	else {
		t.id = 0;
		r = lib_treeof(dummyfs_object_t, linkage, lib_rbFindEx(process->resources.root, &t.linkage, resource_gapcmp));
		if (r != NULL) {
			if (r->lmaxgap > 0)
				*id = r->id - 1;
			else
				*id = r->id + 1;
		}
		else {
			proc_lockClear(&process->lock);
			return NULL;
		}
	}

	r = (dummyfs_object_t *)vm_kmalloc(sizeof(dummyfs_object_t));
	r->id = *id;
	r->refs = 1;

	lib_rbInsert(&process->resources, &r->linkage);
	proc_lockClear(&process->lock);

	return r;
}


int object_destroy(dummyfs_object_t *r)
{
	process_t *process;

	process = proc_current()->process;

	proc_lockSet(&process->lock);

	if (r->refs > 1) {
		proc_lockClear(&process->lock);
		return -EBUSY;
	}

	lib_rbRemove(&process->resources, &r->linkage);
	proc_lockClear(&process->lock);

	vm_kfree(r);

	return EOK;
}


dummyfs_object_t *resource_get(process_t *process, unsigned int id)
{
	dummyfs_object_t *r, t;

	t.id = id;

	proc_lockSet(&process->lock);
	if ((r = lib_treeof(dummyfs_object_t, linkage, lib_rbFind(&process->resources, &t.linkage))) != NULL)
		r->refs++;
	proc_lockClear(&process->lock);

	return r;
}


void resource_put(process_t *process, dummyfs_object_t *r)
{
	proc_lockSet(&process->lock);
	if (r->refs)
		r->refs--;
	proc_lockClear(&process->lock);
	return;
}


int proc_resourceFree(unsigned int h)
{
	process_t *process;
	dummyfs_object_t *r;

	process = proc_current()->process;
	r = resource_get(process, h);

	return resource_free(r);
}


void resource_init(process_t *process)
{
	lib_rbInit(&process->resources, resource_cmp, resource_augment);
}
