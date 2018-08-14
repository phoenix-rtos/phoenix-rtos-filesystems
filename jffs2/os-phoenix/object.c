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

#define JFFS2_MAX_CNT 4096

typedef struct _jffs2_objects_t {
	handle_t	lock;
	rbtree_t	tree;
	u32			cnt;
	struct list_head lru_list;
} jffs2_objects_t;


static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	jffs2_object_t *o1 = lib_treeof(jffs2_object_t, node, n1);
	jffs2_object_t *o2 = lib_treeof(jffs2_object_t, node, n2);

	/* possible overflow */
	return (o1->oid.id - o2->oid.id);
}


int object_remove(void *ptr, jffs2_object_t *o)
{
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)ptr)->objects;

	mutexLock(jffs2_objects->lock);

	lib_rbRemove(&jffs2_objects->tree, &o->node);
	jffs2_objects->cnt--;

	mutexUnlock(jffs2_objects->lock);

	return EOK;
}


void object_destroy(void *ptr, jffs2_object_t *o)
{
	object_remove(ptr, o);
	free(o);
}


jffs2_object_t *object_create(void *ptr, int type, struct inode *inode)
{
	jffs2_object_t *r, *o, t;
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)ptr)->objects;

	mutexLock(jffs2_objects->lock);
	t.oid.id = inode->i_ino;

	r = lib_treeof(jffs2_object_t, node, lib_rbFind(&jffs2_objects->tree, &t.node));
	if (r != NULL) {
		r->refs++;
		list_del_init(&r->list);
		mutexUnlock(jffs2_objects->lock);
		return r;
	}
	r = malloc(sizeof(jffs2_object_t));
	if (r == NULL) {
		printf("jffs2: End of memory - this is very bad\n");
		return NULL;
	}
	memset(r, 0, sizeof(jffs2_object_t));
	r->oid.id = inode->i_ino;
	r->refs = 1;
	r->inode = inode;
	INIT_LIST_HEAD(&r->list);

	while (jffs2_objects->cnt >= JFFS2_MAX_CNT) {
		if (list_empty(&jffs2_objects->lru_list)) {
			printf("jffs2: max files reached, lru is empty no space to free\n");
			free(r);
			return NULL;
		}
		o = list_last_entry(&jffs2_objects->lru_list, jffs2_object_t, list);
		list_del_init(&o->list);
		lib_rbRemove(&jffs2_objects->tree, &o->node);

		free(o->inode->i_mapping);
		o->inode->i_sb->s_op->evict_inode(o->inode);
		o->inode->i_sb->s_op->destroy_inode(o->inode);

		free(o);
		jffs2_objects->cnt--;
	}

	lib_rbInsert(&jffs2_objects->tree, &r->node);
	jffs2_objects->cnt++;
	mutexUnlock(jffs2_objects->lock);

	return r;
}


jffs2_object_t *object_get(void *ptr, unsigned int id, int iref)
{
	jffs2_object_t *o, t;
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)ptr)->objects;

	t.oid.id = id;

	mutexLock(jffs2_objects->lock);
	if ((o = lib_treeof(jffs2_object_t, node, lib_rbFind(&jffs2_objects->tree, &t.node))) != NULL && iref)
		o->refs++;

	if (o != NULL && !list_empty(&o->list))
		list_del_init(&o->list);

	mutexUnlock(jffs2_objects->lock);
	return o;
}


void object_put(void *ptr, jffs2_object_t *o)
{
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)ptr)->objects;

	if (o->refs > 0)
		o->refs--;

	if (o->refs == 0)
		list_add(&o->list, &jffs2_objects->lru_list);
}


void object_init(void **ptr)
{
	jffs2_objects_t *jffs2_objects = malloc(sizeof(jffs2_objects_t));

	lib_rbInit(&jffs2_objects->tree, object_cmp, NULL);
	mutexCreate(&jffs2_objects->lock);
	INIT_LIST_HEAD(&jffs2_objects->lru_list);

	*ptr = jffs2_objects;
}
