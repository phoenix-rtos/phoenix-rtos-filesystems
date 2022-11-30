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

#include "../phoenix-rtos.h"

#define JFFS2_MAX_CNT 4096

typedef struct _jffs2_objects_t {
	handle_t	lock;
	rbtree_t	tree;
	uint32_t	cnt;
	struct list_head lru_list;
} jffs2_objects_t;


static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	jffs2_object_t *o1 = lib_treeof(jffs2_object_t, node, n1);
	jffs2_object_t *o2 = lib_treeof(jffs2_object_t, node, n2);

	/* possible overflow */
	return (o1->oid.id - o2->oid.id);
}


static void _object_destroy(jffs2_objects_t *jffs2_objects, jffs2_object_t *o)
{
	lib_rbRemove(&jffs2_objects->tree, &o->node);
	jffs2_objects->cnt--;
	free(o->inode->i_mapping);
	exit_rwsem(&o->inode->i_rwsem);
	resourceDestroy(o->inode->i_lock);
	o->inode->i_sb->s_op->destroy_inode(o->inode);
	free(o);
}


static jffs2_object_t *_object_create(void *part, struct inode *inode)
{
	jffs2_object_t *r, *o;
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)part)->objects;

	r = malloc(sizeof(jffs2_object_t));
	if (r == NULL) {
		printf("jffs2: End of memory - this is very bad\n");
		return NULL;
	}
	memset(r, 0, sizeof(jffs2_object_t));
	r->oid.id = inode->i_ino;
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
		/* TODO: should we evict inodes from lru_list (they have directory entries pointing to them)? */
		o->inode->i_sb->s_op->evict_inode(o->inode);
		_object_destroy(jffs2_objects, o);
	}

	lib_rbInsert(&jffs2_objects->tree, &r->node);
	jffs2_objects->cnt++;

	return r;
}


int object_insert(void *part, struct inode *inode)
{
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)part)->objects;
	jffs2_object_t *o;

	mutexLock(jffs2_objects->lock);
	mutexLock(inode->i_lock);

	o = _object_create(part, inode);
	if (o == NULL) {
		mutexUnlock(jffs2_objects->lock);
		return -ENOMEM;
	}

	mutexUnlock(jffs2_objects->lock);

	return 0;
}


jffs2_object_t *object_get(void *part, unsigned int id, int create)
{
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)part)->objects;
	jffs2_object_t *o, t;
	struct inode *inode;

	t.oid.id = id;
	mutexLock(jffs2_objects->lock);

	if ((o = lib_treeof(jffs2_object_t, node, lib_rbFind(&jffs2_objects->tree, &t.node))) != NULL) {
		mutexLock(o->inode->i_lock);
		o->inode->i_count++;
		mutexUnlock(o->inode->i_lock);
	}

	if (o == NULL && create) {
		inode = new_inode(((jffs2_partition_t *)part)->sb);
		if (inode != NULL) {
			inode->i_ino = id;
			mutexLock(inode->i_lock);
			o = _object_create((jffs2_partition_t *)part, inode);
		}
	}

	if (o != NULL && !list_empty(&o->list)) {
		list_del_init(&o->list);
	}

	mutexUnlock(jffs2_objects->lock);

	return o;
}


void object_put(void *part, unsigned int id)
{
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)part)->objects;
	jffs2_object_t *o, t;
	int evict = 0;

	t.oid.id = id;
	mutexLock(jffs2_objects->lock);

	if ((o = lib_treeof(jffs2_object_t, node, lib_rbFind(&jffs2_objects->tree, &t.node))) != NULL) {
		mutexLock(o->inode->i_lock);

		if (o->inode->i_count > 0) {
			o->inode->i_count--;
		}

		/* The inode has no in-memory references */
		if (o->inode->i_count == 0) {
			/* There is no directory entry pointing to the inode */
			/* It shall be evicted and destroyed */
			if (o->inode->i_nlink == 0) {
				evict = 1;
			}
			/* Keep the inode in lru_list */
			else {
				list_add(&o->list, &jffs2_objects->lru_list);
			}
		}

		mutexUnlock(o->inode->i_lock);

		if (evict) {
			o->inode->i_sb->s_op->evict_inode(o->inode);
			_object_destroy(jffs2_objects, o);
		}
	}

	mutexUnlock(jffs2_objects->lock);
}


void object_done(void *part)
{
	jffs2_objects_t *jffs2_objects = ((jffs2_partition_t *)part)->objects;
	rbnode_t *node, *next;
	jffs2_object_t *o;

	node = lib_rbMinimum(jffs2_objects->tree.root);
	while (node != NULL) {
		o = lib_treeof(jffs2_object_t, node, node);
		next = lib_rbNext(node);

		_object_destroy(jffs2_objects, o);

		node = next;
	}

	resourceDestroy(jffs2_objects->lock);
	free(jffs2_objects);
}


void object_init(void *part)
{
	jffs2_objects_t *jffs2_objects = malloc(sizeof(jffs2_objects_t));
	memset(jffs2_objects, 0, sizeof(jffs2_objects_t));

	lib_rbInit(&jffs2_objects->tree, object_cmp, NULL);
	mutexCreate(&jffs2_objects->lock);
	INIT_LIST_HEAD(&jffs2_objects->lru_list);

	((jffs2_partition_t *)part)->objects = jffs2_objects;
}
