/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * ext2.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/file.h>
#include <sys/threads.h>
#include <dirent.h>
#include <phoenix/sysinfo.h>

#include "ext2.h"
#include "inode.h"
#include "file.h"
#include "sb.h"
#include "dir.h"
#include "inode.h"
#include "object.h"
#include <phoenix/msg.h>


static int ext2_destroy(ext2_fs_info_t *f, id_t *id);
static int ext2_link(ext2_fs_info_t *f, id_t *dirId, const char *name, const size_t len, id_t *id);


static int ext2_lookup(ext2_fs_info_t *f, id_t *id, const char *name, const size_t len, id_t *resId, mode_t *mode)
{
	int err;
	ext2_object_t *d;

	if (*id < 2)
		return -EINVAL;

	if (len == 0)
		return -ENOENT;

	d = object_get(f, id);

	if (d == NULL)
		return -ENOENT;

	mutexLock(d->lock);
	err = dir_find(d, name, len, resId);
	mutexUnlock(d->lock);

	object_put(d);

	return err;
}


static int ext2_setattr(ext2_fs_info_t *f, id_t *id, int type, int attr, const void *data, size_t size)
{
	ext2_object_t *o = object_get(f, id);
	int res = EOK;

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

		case atMode:
			o->inode->mode |= (attr & 0x1FF);
			break;

		case atUid:
			o->inode->uid = attr;
			break;

		case atGid:
			o->inode->gid = attr;
			break;

		case atSize:
			mutexUnlock(o->lock);
			ext2_truncate(f, id, attr);
			mutexLock(o->lock);
			break;
	}

	o->inode->mtime = o->inode->atime = time(NULL);
	o->dirty = 1;
	object_sync(o);
	mutexUnlock(o->lock);
	object_put(o);
	return res;
}


static int ext2_getattr(ext2_fs_info_t *f, id_t *id, int type, void *attr, size_t maxlen)
{
	ext2_object_t *o = object_get(f, id);
	struct stat *stat;
	ssize_t ret = 0;

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

		case atMode:
			*(int *)attr = o->inode->mode;
			ret = sizeof(int);
			break;

		case atStatStruct:
			stat = (struct stat *)attr;
			//stat->st_dev = o->port;
			stat->st_ino = o->id;
			stat->st_mode = o->inode->mode;
			stat->st_nlink = o->inode->nlink;
			stat->st_uid = o->inode->uid;
			stat->st_gid = o->inode->gid;
			stat->st_size = o->inode->size;
			stat->st_atime = o->inode->atime;
			stat->st_mtime = o->inode->mtime;
			stat->st_ctime = o->inode->ctime;
			ret = sizeof(struct stat);
			break;
	}

	mutexUnlock(o->lock);
	object_put(o);
	return ret;
}


static int ext2_create(ext2_fs_info_t *f, id_t *dirId, const char *name, const size_t len, id_t *resId, int mode, oid_t *dev)
{

	ext2_object_t *o;
	ext2_inode_t *inode = NULL;
	int ret;

	if (name == NULL || strlen(name) == 0)
		return -EINVAL;

	o = object_create(f, resId, dirId, &inode, mode);

	if (o == NULL) {
		if (inode == NULL)
			return -ENOSPC;

		free(inode);
		return -ENOMEM;
	}

	o->inode->ctime = o->inode->mtime = o->inode->atime = time(NULL);

	if ((ret = ext2_link(f, dirId, name, len, resId)) != EOK) {
		object_put(o);
		ext2_destroy(f, resId);
		return ret;
	}
	object_put(o);

	return EOK;
}


static int ext2_destroy(ext2_fs_info_t *f, id_t *id)
{
	ext2_object_t *o = object_get(f, id);

	if (o == NULL)
		return -EINVAL;

	object_sync(o);
	ext2_truncate(f, id, 0);
	object_destroy(o);

	return EOK;
}


static int ext2_link(ext2_fs_info_t *f, id_t *dirId, const char *name, const size_t len, id_t *id)
{
	ext2_object_t *d, *o;
	int res;

	if (dirId == NULL || id == NULL)
		return -EINVAL;

	if (*dirId < 2 || *id < 2)
		return -EINVAL;

	d = object_get(f, dirId);
	o = object_get(f, id);

	if (o == NULL || d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & S_IFDIR)) {
		object_put(o);
		object_put(d);
		return -ENOTDIR;
	}

	if (dir_find(d, name, len, id) == EOK) {
		object_put(o);
		object_put(d);
		return -EEXIST;
	}

	if((o->inode->mode & S_IFDIR) && o->inode->nlink) {
		object_put(o);
		object_put(d);
		return -EMLINK;
	}

	mutexLock(d->lock);
	if ((res = dir_add(d, name, len, o->inode->mode, id)) == EOK) {

		mutexUnlock(d->lock);

		mutexLock(o->lock);
		o->inode->nlink++;
		o->inode->uid = 0;
		o->inode->gid = 0;
		o->inode->mtime = o->inode->atime = time(NULL);
		o->dirty = 1;

		if(o->inode->mode & S_IFDIR) {
			dir_add(o, ".", 1, S_IFDIR, id);
			o->inode->nlink++;
			dir_add(o, "..", 2, S_IFDIR, dirId);
			object_sync(o);
			mutexUnlock(o->lock);

			mutexLock(d->lock);
			d->inode->nlink++;
			d->dirty = 1;
			object_sync(d);
			mutexUnlock(d->lock);

			object_put(o);
			object_put(d);
			return res;
		}

		object_sync(o);
		mutexUnlock(o->lock);
		object_put(o);
		object_put(d);
		return res;
	}

	mutexUnlock(d->lock);
	object_put(o);
	object_put(d);
	return res;
}


static int ext2_unlink(ext2_fs_info_t *f, id_t *dirId, const char *name, const size_t len)
{
	ext2_object_t *d, *o;
	id_t id;

	d = object_get(f, dirId);

	if (d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & S_IFDIR)) {
		object_put(d);
		return -ENOTDIR;
	}

	mutexLock(d->lock);

	if (dir_find(d, name, len, &id) != EOK) {
		mutexUnlock(d->lock);
		object_put(d);
		return -ENOENT;
	}

	mutexUnlock(d->lock);

	o = object_get(f, &id);

	if (o == NULL) {
		dir_remove(d, name);
		object_put(d);
		return -ENOENT;
	}

	if (dir_remove(d, name) != EOK) {
		mutexUnlock(d->lock);
		object_put(o);
		return -ENOENT;
	}

	o->inode->nlink--;
	if (o->inode->mode & S_IFDIR) {
		d->inode->nlink--;
		o->inode->nlink--;
		ext2_destroy(f, &id);
		object_put(d);
		return EOK;
	}

	if (!o->inode->nlink)
		ext2_destroy(f, &id);
	else
		o->inode->mtime = o->inode->atime = time(NULL);

	object_put(d);
	return EOK;
}



static int ext2_readdir(ext2_fs_info_t *f, id_t *dirId, offs_t offs, struct dirent *dent, unsigned int size)
{
	ext2_object_t *d;
	ext2_dir_entry_t *dentry;
	int err;
	int coffs = 0;

	d = object_get(f, dirId);

	if (d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & S_IFDIR)) {
		object_put(d);
		return -ENOTDIR;
	}

	if (!d->inode->nlink) {
		object_put(d);
		return -ENOENT;
	}

	if (!d->inode->size) {
		object_put(d);
		return -ENOENT;
	}

	dentry = malloc(size);

	mutexLock(d->lock);
	while (offs < d->inode->size) {
		ext2_read(f, dirId, offs, (void *)dentry, size, &err);
		mutexUnlock(d->lock);

		dent->d_ino = dentry->inode;
		dent->d_reclen = dentry->rec_len + coffs;
		dent->d_namlen = dentry->name_len;
		if (dentry->name_len == 0) {
			offs += dent->d_reclen;
			coffs += dent->d_reclen;
			if (dentry->rec_len > 0)
				continue;
			else break;
		} else if (!dentry->rec_len)
			break;

		dent->d_type = dentry->file_type & EXT2_FT_DIR ? 0 : 1;
		memcpy(&(dent->d_name[0]), dentry->name, dentry->name_len);
		dent->d_name[dentry->name_len] = '\0';

		free(dentry);
		object_put(d);
		return 	EOK;
	}
	d->inode->atime = time(NULL);
	mutexUnlock(d->lock);

	free(dentry);
	return -ENOENT;
}


static int ext2_open(ext2_fs_info_t *f, id_t *id)
{
	ext2_object_t *o =object_get(f, id);
	if (o != NULL) {
		o->inode->atime = time(NULL);
		return -EOK;
	}
	return -EINVAL;
}


static int ext2_close(ext2_fs_info_t *f, id_t *id)
{
	ext2_object_t *o = object_get(f, id);

	if (!o)
		return -EINVAL;

	mutexLock(o->lock);
	object_sync(o);
	mutexUnlock(o->lock);
	object_put(o);
	object_put(o);

	return EOK;
}

int ext2_message_handler(void *data, msg_t *msg)
{
	int err;
	ext2_fs_info_t *f = (ext2_fs_info_t *)data;

	switch (msg->type) {

		case mtLookup:

			err = ext2_lookup(f, &msg->object, msg->i.data, msg->i.size, &msg->o.lookup.id, &msg->o.lookup.mode);
			if ((err == -ENOENT) && (msg->i.lookup.flags & O_CREAT)) {
				err = ext2_create(f, &msg->object, msg->i.data, msg->i.size, &msg->o.lookup.id, msg->i.lookup.mode, &msg->i.lookup.dev);
				if (!err)
					msg->o.lookup.mode = msg->i.lookup.mode;
			}

			if (!err)
				ext2_open(f, &msg->object);
			break;

		case mtRead:
			msg->o.io = ext2_read(f, &msg->object, msg->i.io.offs, msg->o.data, msg->o.size, &err);
			break;

		case mtWrite:
			msg->o.io = ext2_write(f, &msg->object, msg->i.io.offs, msg->i.data, msg->i.size, &err);
			break;

		case mtSetAttr:
			err =ext2_setattr(f, &msg->object, msg->i.attr, *(int *)msg->i.data, NULL, 0);
			break;

		case mtGetAttr:
			err = ext2_getattr(f, &msg->object, msg->i.attr, msg->o.data, msg->o.size);
			break;

		case mtLink:
			err = ext2_link(f, &msg->object, msg->i.data, msg->i.size, &msg->i.link.id);
			break;

		case mtUnlink:
			err = ext2_unlink(f, &msg->object, msg->i.data, msg->i.size);
			break;

		case mtOpen:
			err = ext2_open(f, &msg->object);
			msg->o.open = msg->object;
			break;

		case mtClose:
			err = ext2_close(f, &msg->object);
			break;
	}

	return err;
}
