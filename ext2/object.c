/*
 * Phoenix-RTOS
 *
 * ext2
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

#include <stdint.h>
#include <stdlib.h>
#include <sys/rb.h>
#include <sys/file.h>
#include <sys/threads.h>
#include <errno.h>
#include <string.h>

#include "ext2.h"
#include "block.h"
#include "dir.h"
#include "inode.h"
#include "object.h"


static int object_cmp(rbnode_t *n1, rbnode_t *n2)
{
	ext2_object_t *o1 = lib_treeof(ext2_object_t, node, n1);
	ext2_object_t *o2 = lib_treeof(ext2_object_t, node, n2);

	if (o1->id > o2->id)
		return 1;
	else if (o1->id < o2->id)
		return -1;

	return 0;
}


int object_destroy(ext2_object_t *o)
{
	ext2_object_t t;

	mutexLock(o->f->objects->ulock);

	t.id = o->id;

	if ((o == lib_treeof(ext2_object_t, node, lib_rbFind(&o->f->objects->used, &t.node)))) {
		o->f->objects->used_cnt--;
		lib_rbRemove(&o->f->objects->used, &o->node);
	}

	mutexLock(o->f->objects->clock);
	if (o->f->objects->cache[o->id % EXT2_CACHE_SIZE] == o)
		o->f->objects->cache[o->id % EXT2_CACHE_SIZE] = NULL;
	mutexUnlock(o->f->objects->clock);

	inode_free(o->f, o->id, o->inode);
	free(o->ind[0].data);
	free(o->ind[1].data);
	free(o->ind[2].data);
	/*mutex destroy */
	resourceDestroy(o->lock);
	free(o);

	mutexUnlock(o->f->objects->ulock);

	return EOK;
}


int object_remove(ext2_object_t *o)
{
	ext2_object_t *r;

	mutexLock(o->f->objects->ulock);
	if (o->refs > 0) {
		mutexUnlock(o->f->objects->ulock);
		return -EBUSY;
	}

	lib_rbRemove(&o->f->objects->used, &o->node);
	o->f->objects->used_cnt--;

	mutexLock(o->f->objects->clock);
	r  = o->f->objects->cache[o->id % EXT2_CACHE_SIZE];
	o->f->objects->cache[o->id % EXT2_CACHE_SIZE] = o;

	if (r != NULL && r != o) {
		object_sync(r);
		inode_put(r->inode);
		free(r->ind[0].data);
		free(r->ind[1].data);
		free(r->ind[2].data);
		/*mutex destroy */
		resourceDestroy(r->lock);
		free(r);
	}

	mutexUnlock(o->f->objects->clock);
	mutexUnlock(o->f->objects->ulock);
	return EOK;
}


ext2_object_t *object_create(ext2_fs_info_t *f, id_t *id, id_t *pid, ext2_inode_t **inode, int mode)
{
	ext2_object_t *o, t;

	mutexLock(f->objects->ulock);

	if (*inode == NULL) {
		*inode = malloc(f->inode_size);

		*id = inode_create(f, *inode, mode, *pid);

		if (!*id) {
			free(*inode);
			*inode = NULL;
			return NULL;
		}
	}
	t.id = *id;

	if (f->objects->used_cnt >= EXT2_MAX_FILES) {
		mutexUnlock(f->objects->ulock);
		return NULL;
	}

	if ((o = lib_treeof(ext2_object_t, node, lib_rbFind(&f->objects->used, &t.node))) != NULL) {
		o->refs++;
		mutexUnlock(f->objects->ulock);
		return o;
	}

	o = (ext2_object_t *)malloc(sizeof(ext2_object_t));
	if (o == NULL) {
		mutexUnlock(f->objects->ulock);
		return NULL;
	}

	memset(o, 0, sizeof(ext2_object_t));
	o->refs = 1;
	o->id = *id;
	o->inode = *inode;
	o->dirty = 1;
	mutexCreate(&o->lock);

	lib_rbInsert(&f->objects->used, &o->node);
	f->objects->used_cnt++;

	mutexUnlock(f->objects->ulock);

	return o;
}


ext2_object_t *object_get(ext2_fs_info_t *f, id_t *id)
{
	ext2_object_t *o, t;
	ext2_inode_t *inode;

	t.id = *id;

	mutexLock(f->objects->ulock);
	/* check used/opened inodes tree */
	if ((o = lib_treeof(ext2_object_t, node, lib_rbFind(&f->objects->used, &t.node))) != NULL) {
		o->refs++;
		mutexUnlock(f->objects->ulock);
		return o;
	} else {
		/* check recently closed cache */
		mutexLock(f->objects->clock);
		if ((o = f->objects->cache[*id % EXT2_CACHE_SIZE]) != NULL && o->id == *id) {

			f->objects->cache[*id % EXT2_CACHE_SIZE] = NULL;
			o->refs++;
			lib_rbInsert(&f->objects->used, &o->node);
			f->objects->used_cnt++;
			mutexUnlock(f->objects->clock);
			mutexUnlock(f->objects->ulock);
			return o;
		}
		mutexUnlock(f->objects->clock);
	}
	inode = inode_get(f, *id);

	if (inode != NULL) {

		mutexUnlock(f->objects->ulock);
		o = object_create(f, id, NULL, &inode, inode->mode);
		return o;
	}
	mutexUnlock(f->objects->ulock);

	return o;
}


void object_sync(ext2_object_t *o)
{
	if (o->dirty)
		inode_set(o->f, o->id, o->inode);

	write_block(o->f, o->ind[0].bno, o->ind[0].data);
	write_block(o->f, o->ind[1].bno, o->ind[1].data);
	write_block(o->f, o->ind[2].bno, o->ind[2].data);
	o->dirty = 0;
}


void object_put(ext2_object_t *o)
{
	mutexLock(o->f->objects->ulock);
	if (o != NULL && o->refs)
		o->refs--;

	if(!o->refs) {
		mutexUnlock(o->f->objects->ulock);
		return;
	}
	mutexUnlock(o->f->objects->ulock);

	return;
}


int object_init(ext2_fs_info_t *f)
{
	f->objects = malloc(sizeof(ext2_fs_objects_t));
	if(!f->objects)
		return -ENOMEM;

	lib_rbInit(&f->objects->used, object_cmp, NULL);

	f->objects->used_cnt = 0;
	memset(&f->objects->cache, 0, EXT2_CACHE_SIZE);

	mutexCreate(&f->objects->ulock);
	mutexCreate(&f->objects->clock);
	return EOK;
}
