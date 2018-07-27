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

#include "../os-phoenix.h"

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


int object_remove(jffs2_object_t *o)
{
	mutexLock(jffs2_objects.lock);

	lib_rbRemove(&jffs2_objects.tree, &o->node);

	mutexUnlock(jffs2_objects.lock);

	return EOK;
}


void object_destroy(jffs2_object_t *o)
{
	object_remove(o);
	free(o);
}


jffs2_object_t *object_create(int type, struct inode *inode)
{
	jffs2_object_t *r, t;

	mutexLock(jffs2_objects.lock);
	t.oid.id = inode->i_ino;

	r = lib_treeof(jffs2_object_t, node, lib_rbFind(&jffs2_objects.tree, &t.node));
	if (r != NULL) {
		mutexUnlock(jffs2_objects.lock);
		return NULL;
	}
	r = malloc(sizeof(jffs2_object_t));
	if (r == NULL) {
		printf("End of memory - this is very bad\n");
		return NULL;
	}
	memset(r, 0, sizeof(jffs2_object_t));
	r->oid.id = inode->i_ino;
	r->refs = 1;
	r->inode = inode;

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
	if (o->refs > 0)
		o->refs--;
}


void object_init(void)
{
	lib_rbInit(&jffs2_objects.tree, object_cmp, NULL);
	mutexCreate(&jffs2_objects.lock);
}
