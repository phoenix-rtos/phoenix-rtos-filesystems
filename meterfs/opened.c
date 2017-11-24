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
#include <sys/rb.h>
#include "files.h"


typedef struct {
	rbnode_t linkage;
	size_t lmaxgap;
	size_t rmaxgap;
	unsigned int refs;
	unsigned int id;
	file_t file;
} file_node_t;


static rbtree_t tree;


static int opened_cmp(rbnode_t *n1, rbnode_t *n2)
{
	file_node_t *f1 = lib_treeof(file_node_t, linkage, n1);
	file_node_t *f2 = lib_treeof(file_node_t, linkage, n2);

	return f2->id - f1->id;
}


static void opened_augment(rbnode_t *node)
{
	rbnode_t *it;
	rbnode_t *parent = node->parent;
	file_node_t *n = lib_treeof(file_node_t, linkage, node);
	file_node_t *p = lib_treeof(file_node_t, linkage, parent);
	file_node_t *pp = (parent != NULL) ? lib_treeof(file_node_t, linkage, parent->parent) : NULL;
	file_node_t *l, *r;

	if (node->left == NULL) {
		if (parent != NULL && parent->right == node)
			n->lmaxgap = n->id - p->id - 1;
		else if (parent != NULL && parent->parent != NULL && parent->parent->right == parent)
			n->lmaxgap = n->id - pp->id - 1;
		else
			n->lmaxgap = n->id;
	}
	else {
		l = lib_treeof(file_node_t, linkage, node->left);
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
		r = lib_treeof(file_node_t, linkage, node->right);
		n->rmaxgap = max(r->lmaxgap, r->rmaxgap);
	}

	for (it = node; it->parent != NULL; it = it->parent) {
		n = lib_treeof(file_node_t, linkage, it);
		p = lib_treeof(file_node_t, linkage, it->parent);

		if (it->parent->left == it)
			p->lmaxgap = max(n->lmaxgap, n->rmaxgap);
		else
			p->rmaxgap = max(n->lmaxgap, n->rmaxgap);
	}
}


static int opened_gapcmp(rbnode_t *n1, rbnode_t *n2)
{
	file_node_t *r1 = lib_treeof(file_node_t, linkage, n1);
	file_node_t *r2 = lib_treeof(file_node_t, linkage, n2);
	rbnode_t *child = NULL;
	int ret = 1;

	if (r1->lmaxgap > 0 && r1->rmaxgap > 0) {
		if (r2->id > r1->id) {
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
		return 0;

	return ret;
}


int opened_add(file_t *file, unsigned int *id)
{
	file_node_t *r, t;

	if (tree.root == NULL) {
		*id = 0;
	}
	else {
		t.id = 0;
		if ((r = lib_treeof(file_node_t, linkage, lib_rbFindEx(tree.root, &t.linkage, opened_gapcmp))) == NULL)
			return -1;

		if (r->lmaxgap > 0)
			*id = r->id - 1;
		else
			*id = r->id + 1;
	}

	r = malloc(sizeof(file_node_t));
	r->id = *id;
	r->refs = 1;
	memcpy(&r->file, file, sizeof(file_t));

	lib_rbInsert(&tree, &r->linkage);

	return 0;
}


int opened_remove(unsigned int id)
{
	file_node_t *r, t;

	t.id = id;

	if ((r = lib_treeof(file_node_t, linkage, lib_rbFind(&tree, &t.linkage))) == NULL)
		return -1;

	if (r->refs > 0) {
		--(r->refs);
		return 0;
	}

	lib_rbRemove(&tree, &r->linkage);

	free(r);

	return 0;
}


file_t *opened_find(unsigned int id)
{
	file_node_t t, *p;

	t.id = id;

	if ((p = lib_treeof(file_node_t, linkage, lib_rbFind(&tree, &t.linkage))) == NULL)
		return NULL;

	return &p->file;
}


int opened_claim(const char *name, unsigned int *id)
{
	file_node_t *p;

	if (tree.root == NULL)
		return -1;

	p = lib_treeof(file_node_t, linkage, lib_rbMinimum(tree.root));

	while (p != NULL) {
		if (strncmp(name, p->file.header.name, sizeof(p->file.header.name)) == 0) {
			(*id) = p->id;
			++(p->refs);
			return 0;
		}

		p = lib_treeof(file_node_t, linkage, lib_rbNext(&p->linkage));
	}

	return -1;
}


void opened_init(void)
{
	lib_rbInit(&tree, opened_cmp, opened_augment);
}
