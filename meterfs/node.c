/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Opened files
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/rb.h>
#include "files.h"


enum { NODE_FILE = 0, NODE_MOUNT };


typedef struct {
	rbnode_t linkage;
	size_t lmaxgap;
	size_t rmaxgap;
	int refs;
	oid_t oid;
	char type;
	union {
		file_t file;
		char name[sizeof(file_t)];
	};
} node_t;


static rbtree_t tree;


static int node_cmp(rbnode_t *n1, rbnode_t *n2)
{
	node_t *f1 = lib_treeof(node_t, linkage, n1);
	node_t *f2 = lib_treeof(node_t, linkage, n2);

	return f1->oid.id - f2->oid.id;
}


static void node_augment(rbnode_t *node)
{
	rbnode_t *it;
	rbnode_t *parent = node->parent;
	node_t *n = lib_treeof(node_t, linkage, node);
	node_t *p = lib_treeof(node_t, linkage, parent);
	node_t *pp = (parent != NULL) ? lib_treeof(node_t, linkage, parent->parent) : NULL;
	node_t *l, *r;

	if (node->left == NULL) {
		if (parent != NULL && parent->right == node)
			n->lmaxgap = n->oid.id - p->oid.id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->right == parent)
			n->lmaxgap = n->oid.id - pp->oid.id - 1;
		else
			n->lmaxgap = n->oid.id;
	}
	else {
		l = lib_treeof(node_t, linkage, node->left);
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
		r = lib_treeof(node_t, linkage, node->right);
		n->rmaxgap = max(r->lmaxgap, r->rmaxgap);
	}

	for (it = node; it->parent != NULL; it = it->parent) {
		n = lib_treeof(node_t, linkage, it);
		p = lib_treeof(node_t, linkage, it->parent);

		if (it->parent->left == it)
			p->lmaxgap = max(n->lmaxgap, n->rmaxgap);
		else
			p->rmaxgap = max(n->lmaxgap, n->rmaxgap);
	}
}


static int node_gapcmp(rbnode_t *n1, rbnode_t *n2)
{
	node_t *r1 = lib_treeof(node_t, linkage, n1);
	node_t *r2 = lib_treeof(node_t, linkage, n2);
	rbnode_t *child = NULL;
	int ret = 1;

	if (r1->lmaxgap > 0 && r1->rmaxgap > 0) {
		if (r2->oid.id > r1->oid.id) {
			child = n1->right;
			ret = -1;
		}
		else {
			child = n1->left;
			ret = 1;
		}
	}
	else if (r1->lmaxgap > 0) {
		child = n1->left;
		ret = 1;
	}
	else if (r1->rmaxgap > 0) {
		child = n1->right;
		ret = -1;
	}

	if (child == NULL)
		return EOK;

	return ret;
}


int node_add(file_t *file, const char *name, oid_t *oid)
{
	node_t *r, t;

	if (file != NULL) {
		t.oid = *oid;
		if ((r = lib_treeof(node_t, linkage, lib_rbFind(&tree, &t.linkage))) != NULL)
			return EOK; /* File is already in the tree, nothing to do */

		if ((r = malloc(sizeof(node_t))) == NULL)
			return -ENOMEM;

		r->refs = 1;
		r->type = NODE_FILE;
		r->oid = *oid;
		memcpy(&r->file, file, sizeof(file_t));

		lib_rbInsert(&tree, &r->linkage);

		return EOK;
	}
	else if (name == NULL) {
		return -EINVAL;
	}

	if (tree.root == NULL) {
		oid->id = 1 << ((sizeof(oid->id) << 3) - 1); /* Set MSB of id */
	}
	else {
		t.oid.id = 1 << ((sizeof(t.oid.id) << 3) - 1); /* Set MSB of id */
		if ((r = lib_treeof(node_t, linkage, lib_rbFindEx(tree.root, &t.linkage, node_gapcmp))) == NULL)
			return -1;

		if (r->rmaxgap > 0)
			oid->id = r->oid.id + 1;
		else
			oid->id = r->oid.id - 1;

		if (oid->id < t.oid.id)
			return -ENOMEM;
	}

	if ((r = malloc(sizeof(node_t))) == NULL)
		return -ENOMEM;

	r->oid = *oid;
	r->refs = 1;

	if (file != NULL) {
		r->type = NODE_FILE;
		memcpy(&r->file, file, sizeof(file_t));
	}
	else {
		r->type = NODE_MOUNT;
		strncpy(r->name, name, sizeof(r->name));
	}

	lib_rbInsert(&tree, &r->linkage);

	return EOK;
}


int node_remove(unsigned int id)
{
	node_t *r, t;

	t.oid.id = id;

	if ((r = lib_treeof(node_t, linkage, lib_rbFind(&tree, &t.linkage))) == NULL)
		return -EINVAL;

	if ((--r->refs) > 0)
		return EOK;

	lib_rbRemove(&tree, &r->linkage);

	free(r);

	return EOK;
}


file_t *node_findFile(unsigned int id)
{
	node_t t, *p;

	t.oid.id = id;

	if ((p = lib_treeof(node_t, linkage, lib_rbFind(&tree, &t.linkage))) == NULL)
		return NULL;

	return &p->file;
}


int node_findMount(oid_t *oid, const char *name)
{
	node_t *p;

	p = lib_treeof(node_t, linkage, lib_rbMinimum(tree.root));

	while (p != NULL) {
		if (p->type == NODE_MOUNT) {
			if (strcmp(p->name, name) == 0) {
				*oid = p->oid;
				return EOK;
			}
		}

		p = lib_treeof(node_t, linkage, lib_rbNext(&p->linkage));
	}

	return -ENOENT;
}


int node_claim(const char *name, unsigned int *id)
{
	node_t *p;

	if (tree.root == NULL)
		return -1;

	p = lib_treeof(node_t, linkage, lib_rbMinimum(tree.root));

	while (p != NULL) {
		if (p->type == NODE_FILE && strncmp(name, p->file.header.name, sizeof(p->file.header.name)) == 0) {
			(*id) = p->oid.id;
			++(p->refs);
			return EOK;
		}

		p = lib_treeof(node_t, linkage, lib_rbNext(&p->linkage));
	}

	return -ENOENT;
}


void node_cleanAll(void)
{
	node_t *p;

	p = lib_treeof(node_t, linkage, lib_rbMinimum(tree.root));

	while (p != NULL) {
		if (p->type == NODE_FILE) {
			lib_rbRemove(&tree, &p->linkage);
			free(p);
			p = lib_treeof(node_t, linkage, lib_rbMinimum(tree.root));
		}
		else {
			p = lib_treeof(node_t, linkage, lib_rbNext(&p->linkage));
		}
	}
}


void node_init(void)
{
	lib_rbInit(&tree, node_cmp, node_augment);
}
