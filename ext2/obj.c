/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Filesystem object
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/list.h>
#include <sys/stat.h>
#include <sys/threads.h>

#include "block.h"
#include "file.h"
#include "obj.h"


/* Releases object resources and removes it from objects in use */
static int _ext2_obj_remove(ext2_t *fs, ext2_obj_t *obj)
{
	int err;

	if ((err = resourceDestroy(obj->lock)) < 0)
		return err;

	free(obj->inode);
	free(obj->ind[0].data);
	free(obj->ind[1].data);
	free(obj->ind[2].data);

	lib_rbRemove(&fs->objs->used, &obj->node);
	fs->objs->count--;

	return EOK;
}


/* Destroys object */
static int _ext2_obj_destroy(ext2_t *fs, ext2_obj_t *obj, bool ignoreSync)
{
	int err = ext2_inode_destroy(fs, (uint32_t)obj->id, obj->inode->mode);
	if ((err < 0) && (!ignoreSync)) {
		return err;
	}

	err = _ext2_obj_remove(fs, obj);
	if (err < 0) {
		return err;
	}

	free(obj);

	return EOK;
}


/* Removes object from LRU cache */
static int _ext2_obj_removelru(ext2_t *fs)
{
	ext2_obj_t *obj;
	int err;

	if ((obj = fs->objs->lru) == NULL)
		return -ENOENT;

	if ((err = ext2_obj_sync(fs, obj)) < 0)
		return err;

	if ((err = _ext2_obj_remove(fs, obj)) < 0)
		return err;

	LIST_REMOVE(&fs->objs->lru, obj);
	free(obj);

	return EOK;
}


/* Creates new object */
static int _ext2_obj_create(ext2_t *fs, uint32_t pino, ext2_inode_t *inode, uint16_t mode, ext2_obj_t **res)
{
	ext2_obj_t *obj;
	uint32_t ino;
	int err;

	if (inode == NULL) {
		if (!(ino = ext2_inode_create(fs, pino, mode)))
			return -ENOSPC;

		if ((inode = (ext2_inode_t *)malloc(fs->sb->inodesz)) == NULL) {
			ext2_inode_destroy(fs, ino, mode);
			return -ENOMEM;
		}

		memset(inode, 0, fs->sb->inodesz);
		inode->ctime = inode->mtime = inode->atime = time(NULL);
		inode->mode = mode;
	}
	else {
		ino = pino;
	}

	do {
		if ((fs->objs->count >= MAX_OBJECTS) && ((err = _ext2_obj_removelru(fs)) < 0))
			break;

		if ((*res = obj = (ext2_obj_t *)malloc(sizeof(ext2_obj_t))) == NULL) {
			err = -ENOMEM;
			break;
		}

		memset(obj, 0, sizeof(ext2_obj_t));

		if ((err = mutexCreate(&obj->lock)) < 0) {
			free(obj);
			*res = NULL;
			break;
		}

		obj->id = ino;
		obj->refs = 1;
		obj->flags = OFLAG_DIRTY;
		obj->inode = inode;
		obj->prev = NULL;
		obj->next = NULL;

		lib_rbInsert(&fs->objs->used, &obj->node);
		fs->objs->count++;

		return EOK;
	} while (0);

	free(inode);
	ext2_inode_destroy(fs, ino, mode);

	return err;
}


ext2_obj_t *ext2_obj_get(ext2_t *fs, id_t id)
{
	ext2_obj_t *obj, tmp;
	ext2_inode_t *inode;

	mutexLock(fs->objs->lock);
	do {
		tmp.id = id;
		obj = lib_treeof(ext2_obj_t, node, lib_rbFind(&fs->objs->used, &tmp.node));
		if (obj != NULL) {
			obj->refs++;
			if ((obj->refs == 1) && !EXT2_IS_MOUNTPOINT(obj)) {
				LIST_REMOVE(&fs->objs->lru, obj);
			}
			break;
		}

		if ((inode = ext2_inode_init(fs, (uint32_t)id)) == NULL) {
			break;
		}

		if (_ext2_obj_create(fs, (uint32_t)id, inode, inode->mode, &obj) < 0) {
			break;
		}
	} while (0);

	mutexUnlock(fs->objs->lock);

	return obj;
}


void ext2_obj_put(ext2_t *fs, ext2_obj_t *obj)
{
	mutexLock(fs->objs->lock);

	obj->refs--;
	if ((obj->refs == 0) && !EXT2_IS_MOUNTPOINT(obj)) {
		if (!obj->inode->links) {
			_ext2_obj_destroy(fs, obj, false);
		}
		else {
			LIST_ADD(&fs->objs->lru, obj);
		}
	}

	mutexUnlock(fs->objs->lock);
}


int _ext2_obj_sync(ext2_t *fs, ext2_obj_t *obj)
{
	int err;

	if (EXT2_IS_DIRTY(obj)) {
		if ((err = ext2_inode_sync(fs, (uint32_t)obj->id, obj->inode)) < 0)
			return err;

		obj->flags &= ~OFLAG_DIRTY;
	}
	if (!EXT2_ISDEV(obj->inode->mode) && !EXT2_IS_MOUNTPOINT(obj)) {
		if ((obj->ind[0].data != NULL) && (err = ext2_block_write(fs, obj->ind[0].bno, obj->ind[0].data, 1)) < 0)
			return err;

		if ((obj->ind[1].data != NULL) && (err = ext2_block_write(fs, obj->ind[1].bno, obj->ind[1].data, 1)) < 0)
			return err;

		if ((obj->ind[2].data != NULL) && (err = ext2_block_write(fs, obj->ind[2].bno, obj->ind[2].data, 1)) < 0)
			return err;
	}

	return EOK;
}


int ext2_obj_sync(ext2_t *fs, ext2_obj_t *obj)
{
	int ret;

	mutexLock(obj->lock);

	ret = _ext2_obj_sync(fs, obj);

	mutexUnlock(obj->lock);

	return ret;
}


int ext2_obj_truncate(ext2_t *fs, ext2_obj_t *obj, size_t size)
{
	int err;

	mutexLock(obj->lock);

	do {
		if ((err = _ext2_file_truncate(fs, obj, size)) < 0)
			break;

		if ((err = _ext2_obj_sync(fs, obj)) < 0)
			break;
	} while (0);

	mutexUnlock(obj->lock);

	return err;
}


int ext2_obj_destroy(ext2_t *fs, ext2_obj_t *obj)
{
	int ret;

	mutexLock(fs->objs->lock);

	ret = _ext2_obj_destroy(fs, obj, false);

	mutexUnlock(fs->objs->lock);

	return ret;
}


int ext2_obj_create(ext2_t *fs, uint32_t pino, ext2_inode_t *inode, uint16_t mode, ext2_obj_t **res)
{
	int ret;

	mutexLock(fs->objs->lock);

	ret = _ext2_obj_create(fs, pino, inode, mode, res);

	mutexUnlock(fs->objs->lock);

	return ret;
}


void ext2_objs_destroy(ext2_t *fs)
{
	rbnode_t *node, *next;

	mutexLock(fs->objs->lock);

	for (node = lib_rbMinimum(fs->objs->used.root); node; node = next) {
		next = lib_rbNext(node);
		_ext2_obj_destroy(fs, lib_treeof(ext2_obj_t, node, node), true);
	}

	mutexUnlock(fs->objs->lock);

	resourceDestroy(fs->objs->lock);
	free(fs->objs);
}


static int ext2_obj_cmp(rbnode_t *node1, rbnode_t *node2)
{
	ext2_obj_t *obj1 = lib_treeof(ext2_obj_t, node, node1);
	ext2_obj_t *obj2 = lib_treeof(ext2_obj_t, node, node2);

	if (obj1->id > obj2->id)
		return 1;
	else if (obj1->id < obj2->id)
		return -1;

	return 0;
}


int ext2_objs_init(ext2_t *fs)
{
	ext2_objs_t *objs;
	int err;

	if ((objs = (ext2_objs_t *)malloc(sizeof(ext2_objs_t))) == NULL)
		return -ENOMEM;

	if ((err = mutexCreate(&objs->lock)) < 0) {
		free(objs);
		return err;
	}

	objs->count = 0;
	objs->lru = NULL;
	lib_rbInit(&objs->used, ext2_obj_cmp, NULL);

	fs->objs = objs;
	fs->root = NULL;

	return EOK;
}
