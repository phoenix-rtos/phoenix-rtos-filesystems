/*
 * Phoenix-RTOS
 *
 * Opened files
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Aleksander Kaminski, Pawel Pisarczyk, Hubert Buczynski
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


typedef struct {
	rbnode_t linkage;
	size_t lmaxgap;
	size_t rmaxgap;
	id_t id;
	file_t file;
} node_t;


static int node_cmp(rbnode_t *n1, rbnode_t *n2)
{
	node_t *f1 = lib_treeof(node_t, linkage, n1);
	node_t *f2 = lib_treeof(node_t, linkage, n2);

	if (f1->id > f2->id)
		return 1;
	else if (f1->id < f2->id)
		return -1;
	else
		return 0;
}


int node_add(file_t *file, id_t id, rbtree_t *tree)
{
	node_t *r;

	r = malloc(sizeof(node_t));
	if (r == NULL)
		return -ENOMEM;

	r->id = id;
	memcpy(&r->file, file, sizeof(file_t));

	lib_rbInsert(tree, &r->linkage);

	return 0;
}


file_t *node_getByName(const char *name, id_t *id, rbtree_t *tree)
{
	node_t *p = lib_treeof(node_t, linkage, lib_rbMinimum(tree->root));

	while (p != NULL) {
		if (strncmp(name, p->file.header.name, sizeof(p->file.header.name)) == 0) {
			(*id) = p->id;
			return &p->file;
		}

		p = lib_treeof(node_t, linkage, lib_rbNext(&p->linkage));
	}

	return NULL;
}


file_t *node_getById(id_t id, rbtree_t *tree)
{
	node_t t, *p;

	t.id = id;
	p = lib_treeof(node_t, linkage, lib_rbFind(tree, &t.linkage));

	if (p == NULL)
		return NULL;

	return &p->file;
}


void node_cleanAll(rbtree_t *tree)
{
	node_t *p = lib_treeof(node_t, linkage, tree->root);

	while (p != NULL) {
		lib_rbRemove(tree, &p->linkage);
		free(p);
		p = lib_treeof(node_t, linkage, tree->root);
	}
}


int node_getMaxId(rbtree_t *tree)
{
	node_t *p = lib_treeof(node_t, linkage, lib_rbMaximum(tree->root));

	if (p != NULL)
		return p->id;
	else
		return 0;
}


void node_init(rbtree_t *tree)
{
	lib_rbInit(tree, node_cmp, NULL);
}
