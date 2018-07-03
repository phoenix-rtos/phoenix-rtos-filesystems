/*
 * Phoenix-RTOS
 *
 * Opened files
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Aleksander Kaminski, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <arch.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/rb.h>
#include "files.h"


typedef struct {
	rbnode_t linkage;
	size_t lmaxgap;
	size_t rmaxgap;
	int refs;
	id_t id;
	file_t file;
} node_t;


static rbtree_t tree;


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


int node_add(file_t *file, id_t id)
{
	node_t *r;

	if ((r = malloc(sizeof(node_t))) == NULL)
		return -ENOMEM;

	r->refs = 0;
	r->id = id;
	memcpy(&r->file, file, sizeof(file_t));

	lib_rbInsert(&tree, &r->linkage);

	return EOK;
}


file_t *node_getByName(const char *name, id_t *id)
{
	node_t *p;

	p = lib_treeof(node_t, linkage, lib_rbMinimum(tree.root));

	while (p != NULL) {
		if (strncmp(name, p->file.header.name, sizeof(p->file.header.name)) == 0) {
			(*id) = p->id;
			++(p->refs);
			return &p->file;
		}

		p = lib_treeof(node_t, linkage, lib_rbNext(&p->linkage));
	}

	return NULL;
}


file_t *node_getById(id_t id)
{
	node_t *p, t;

	t.id = id;

	if ((p = lib_treeof(node_t, linkage, lib_rbFind(&tree, &t.linkage))) == NULL)
		return NULL;

	++(p->refs);

	return &p->file;
}


int node_put(id_t id)
{
	node_t *r, t;

	t.id = id;

	if ((r = lib_treeof(node_t, linkage, lib_rbFind(&tree, &t.linkage))) == NULL)
		return -ENOENT;

	if ((--r->refs) <= 0) {
		lib_rbRemove(&tree, &r->linkage);
		free(r);
	}

	return EOK;
}


void node_cleanAll(void)
{
	node_t *p;

	while ((p = lib_treeof(node_t, linkage, tree.root)) != NULL) {
		lib_rbRemove(&tree, &p->linkage);
		free(p);
	}
}


int node_getMaxId(void)
{
	node_t *p;

	if ((p = lib_treeof(node_t, linkage, lib_rbMaximum(tree.root))) != NULL)
		return p->id;
	else
		return 0;
}


void node_init(void)
{
	lib_rbInit(&tree, node_cmp, NULL);
}
