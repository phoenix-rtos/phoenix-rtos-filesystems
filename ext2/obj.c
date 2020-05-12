/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Object
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>

#include "obj.h"


static int ext2_objcmp(rbnode_t *node1, rbnode_t *node2)
{
	ext2_obj_t *obj1 = lib_treeof(ext2_obj_t, node, node1);
	ext2_obj_t *obj2 = lib_treeof(ext2_obj_t, node, node2);

	if (obj1->id > obj2->id)
		return 1;
	else if (obj1->id < obj2->id)
		return -1;

	return 0;
}


static int _ext2_destroy_object(ext2_objs_t *objs, ext2_obj_t *obj)
{
	ext2_obj_t t;

	t.id = obj->id;

	if ((obj == lib_treeof(ext2_obj_t, node, lib_rbFind(&obj->fs->objects->used, &t.node)))) {
		if (obj->fs->objs->count)
			obj->fs->objs->count--;
		else
			debug("ext2: GLOBAL OBJECT COUNTER UNDERFLOW\n");
		lib_rbRemove(&obj->fs->objects->used, &obj->node);
	}

	inode_free(obj->fs, obj->id, obj->inode->mode);

	free(obj->inode);

	free(obj->ind[0].data);
	free(obj->ind[1].data);
	free(obj->ind[2].data);
	/*mutex destroy */
	resourceDestroy(obj->lock);
	free(obj);
	return EOK;
}


int object_destroy(ext2_obj_t *obj)
{
	if (!obj)
		return -EINVAL;

	ext2_objs_t *objects = obj->fs->objects;

	mutexLock(objects->lock);
	_ext2_destroy_object(obj);
	mutexUnlock(objects->lock);

	return EOK;
}


int object_remove(ext2_obj_t *obj)
{
	if (!obj)
		return EOK;

	lib_rbRemove(&obj->fs->objects->used, &obj->node);
	if (obj->fs->objs->count)
		obj->fs->objs->count--;
	else
		debug("ext2: GLOBAL OBJECT COUNTER UNDERFLOW\n");

	object_sync(obj);

	free(obj->inode);

	free(obj->ind[0].data);
	free(obj->ind[1].data);
	free(obj->ind[2].data);
	/*mutex destroy */
	resourceDestroy(obj->lock);
	free(obj);

	return EOK;
}


ext2_obj_t *object_create(ext2_t *fs, id_t *id, id_t pid, ext2_inode_t **inode, int mode)
{
	ext2_obj_t *obj, t;

	mutexLock(fs->objects->lock);

	if (*inode == NULL) {
		*inode = (ext2_inode_t *)malloc(fs->sb->inodesz);
		memset(*inode, 0, fs->sb->inodesz);
		(*inode)->mode = mode;
		*id = ext2_create_inode(fs, pid, mode);

		if (!*id) {
			free(*inode);
			*inode = NULL;
			return NULL;
		}
	}
	t.id = *id;

	if ((obj = lib_treeof(ext2_obj_t, node, lib_rbFind(&fs->objects->used, &t.node))) != NULL) {
		obj->refs++;
		mutexUnlock(fs->objects->lock);
		return obj;
	}

	if (fs->objs->count >= EXT2_MAX_FILES) {
		// TODO: free somebody from lru
		if (!fs->objects->lru) {
			debug("ext2: max files reached, lru is empty no space to free\n");
			/* TODO: fix inode possible leak */
			inode_free(obj->fs, *id, *inode->mode);
			free(*inode);
			return NULL;
		}
		obj = fs->objects->lru;
		LIST_REMOVE(&fs->objects->lru, obj);
		object_remove(obj);
		debug("ext2 max files reached removing\n");
	}

	obj = (ext2_obj_t *)malloc(sizeof(ext2_obj_t));
	if (obj == NULL) {
		mutexUnlock(fs->objects->lock);
		return NULL;
	}

	memset(obj, 0, sizeof(ext2_obj_t));
	obj->refs = 1;
	obj->id = *id;
	obj->inode = *inode;
	object_setFlag(obj, EXT2_FL_DIRTY);
	mutexCreate(&obj->lock);
	obj->fs = fs;

	obj->next = NULL;
	obj->prev = NULL;
	lib_rbInsert(&fs->objects->used, &obj->node);
	fs->objs->count++;

	mutexUnlock(fs->objects->lock);

	return obj;
}


ext2_obj_t *object_get(ext2_t *fs, id_t *id)
{
	ext2_obj_t *obj, t;
	ext2_inode_t *inode = NULL;

	t.id = *id;

	mutexLock(fs->objects->lock);
	/* check used/opened inodes tree */
	if ((obj = lib_treeof(ext2_obj_t, node, lib_rbFind(&fs->objects->used, &t.node))) != NULL) {
		if (!obj->refs)
			LIST_REMOVE(&fs->objects->lru, obj);
		obj->refs++;
		mutexUnlock(fs->objects->lock);
		return obj;
	}
	inode = inode_get(fs, *id);

	if (inode != NULL) {

		mutexUnlock(fs->objects->lock);
		obj = object_create(fs, id, NULL, &inode, inode->mode);
		return obj;
	}
	mutexUnlock(fs->objects->lock);

	return obj;
}


void object_sync(ext2_obj_t *obj)
{

	if (object_checkFlag(obj, EXT2_FL_DIRTY))
		inode_set(obj->fs, obj->id, obj->inode);

	object_clearFlag(obj, EXT2_FL_DIRTY);

	if (object_checkFlag(obj, EXT2_FL_MOUNT))
		return;

	if (obj->ind[0].data)
		write_block(obj->fs, obj->ind[0].bno, obj->ind[0].data);
	if (obj->ind[0].data)
		write_block(obj->fs, obj->ind[1].bno, obj->ind[1].data);
	if (obj->ind[0].data)
		write_block(obj->fs, obj->ind[2].bno, obj->ind[2].data);
}


void object_put(ext2_obj_t *obj)
{
	char buf[64];

	if (!obj)
		return;

	mutexLock(obj->fs->objects->lock);
	if (obj->refs)
		obj->refs--;
	else {
		sprintf(buf, "ext2: REF UNDERFLOW %lld\n", obj->id);
		debug(buf);
	}

	if(!obj->inode->nlink && !obj->refs) {
		_ext2_destroy_object(obj);
		mutexUnlock(obj->fs->objects->lock);
		return;
	}

	if (!obj->refs)
		LIST_ADD(&obj->fs->objects->lru, obj);

	mutexUnlock(obj->fs->objects->lock);

	return;
}


int ext2_init_objs(ext2_t *fs)
{
	ext2_objs_t *objs;
	int err;

	if ((objs = (ext2_objs_t *)malloc(sizeof(ext2_objs_t))) == NULL)
		return -ENOMEM;

	if ((err = mutexCreate(&objs->lock)) < 0)
		return err;

	lib_rbInit(&objs->used, ext2_objcmp, NULL);
	objs->count = 0;
	objs->lru = NULL;

	fs->objs = objs;

	return EOK;
}
