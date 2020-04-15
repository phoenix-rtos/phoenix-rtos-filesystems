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
#include <poll.h>
#include <phoenix/sysinfo.h>
#include <phoenix/msg.h>
#include <phoenix/stat.h>

#include "ext2.h"
#include "libext2.h"
#include "inode.h"
#include "file.h"
#include "sb.h"
#include "dir.h"
#include "inode.h"
#include "object.h"

#define TRACE(msg, ...)
/*
#define TRACE(msg, ...) do { \
	char buf[128]; \
	sprintf(buf, __FILE__ ":%d - " msg "\n", __LINE__, ##__VA_ARGS__ ); \
	debug(buf); \
} while (0)
*/


static int ext2_destroy(ext2_fs_info_t *f, id_t *id);
static int ext2_link(ext2_fs_info_t *f, id_t *dirId, const char *name, const size_t len, id_t *id);


static int ext2_lookup(ext2_fs_info_t *f, id_t *id, const char *name, const size_t len, id_t *resId, mode_t *mode)
{
	int err;
	ext2_object_t *d, *o;

	if (*id < 2)
		return -EINVAL;

	if (len == 0)
		return -ENOENT;

	d = object_get(f, id);

	if (d == NULL)
		return -ENOENT;

	mutexLock(d->lock);
	err = dir_find(d, name, len, resId);
	if (!err) {
		o = object_get(f, resId);
		if (o->id != d->id)
			mutexLock(o->lock);
		*mode = o->inode->mode;
		if (object_checkFlag(o, EXT2_FL_MOUNT)) {
			*mode = S_IFMNT | (o->inode->mode & ~S_IFMT);
		}
		else if (object_checkFlag(d, EXT2_FL_MOUNTPOINT) && len == strlen("..") && !strncmp(name, "..", len)) {
			*resId = d->id;
			*mode = S_IFMNT | (d->inode->mode & ~S_IFMT);
		}

		if (o->id != d->id)
			mutexUnlock(o->lock);
		object_put(o);
	}
	mutexUnlock(d->lock);

	object_put(d);

	return err;
}


static int ext2_setattr(ext2_fs_info_t *f, id_t *id, int type, const void *data, size_t size)
{
	ext2_object_t *o = object_get(f, id);
	int res = EOK;

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

		case atMode:
			o->inode->mode = ((o->inode->mode & S_IFMT) | (*(int *)data & ~S_IFMT));
			break;

		case atUid:
			o->inode->uid = *(int *)data;
			break;

		case atGid:
			o->inode->gid = *(int *)data;
			break;

		case atSize:
			mutexUnlock(o->lock);
			ext2_truncate(f, id, *(int *)data);
			mutexLock(o->lock);
			break;
		case atMount:
			object_sync(o);
			object_setFlag(o, EXT2_FL_MOUNT);
			memcpy(&o->mnt, data, sizeof(oid_t));
			mutexUnlock(o->lock);
			return res;
		case atMountPoint:
			object_setFlag(o, EXT2_FL_MOUNTPOINT);
			memcpy(&o->mnt, data, sizeof(oid_t));
			o->refs++;
			object_get(f, id);
			break;
	}

	o->inode->mtime = o->inode->atime = time(NULL);
	object_setFlag(o, EXT2_FL_DIRTY);
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
		case atMount:
		case atMountPoint:
			*(oid_t *)attr = o->mnt;
			break;
		case atSize:
			*(int *)attr = o->inode->size;
			ret = sizeof(int);
			break;
		case atEvents:
			*(int *)attr = POLLIN | POLLOUT;
			ret = sizeof(int);
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

	if (name == NULL || len == 0)
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
	TRACE("ino %llu ref %u", o->id, o->refs);
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
		object_setFlag(o, EXT2_FL_DIRTY);

		if(o->inode->mode & S_IFDIR) {
			dir_add(o, ".", 1, S_IFDIR, id);
			o->inode->nlink++;
			dir_add(o, "..", 2, S_IFDIR, dirId);
			object_sync(o);
			mutexUnlock(o->lock);

			mutexLock(d->lock);
			d->inode->nlink++;
			object_setFlag(d, EXT2_FL_DIRTY);
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
		TRACE("ino %llu ref %u", o->id, o->refs);
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

	o = object_get(f, &id);

	if (o == NULL) {
		dir_remove(d, name, len);
		object_put(d);
		return -ENOENT;
	}

	if (object_checkFlag(o, EXT2_FL_MOUNTPOINT | EXT2_FL_MOUNT)) {
		mutexUnlock(d->lock);
		object_put(o);
		object_put(d);
		return -EBUSY;
	}

	if (dir_remove(d, name, len) != EOK) {
		mutexUnlock(d->lock);
		object_put(o);
		object_put(d);
		return -ENOENT;
	}

	mutexUnlock(d->lock);

	mutexLock(o->lock);
	o->inode->nlink--;
	if (o->inode->mode & S_IFDIR) {
		/* TODO: check if empty? */
		d->inode->nlink--;
		o->inode->nlink--;
		object_put(o);
		object_put(d);
		return EOK;
	}

	o->inode->mtime = o->inode->atime = time(NULL);
	mutexUnlock(o->lock);

	object_put(o);
	object_put(d);
	return EOK;
}


int ext2_readdir(ext2_object_t *d, off_t offs, struct dirent *dent, size_t size)
{
	ext2_dir_entry_t *dentry;
	int err = EOK, ret = -ENOENT;

	if (d == NULL)
		return -EINVAL;

	if (!d->inode->nlink)
		return -ENOENT;

	if (!d->inode->size)
		return -ENOENT;

	if (size < sizeof(ext2_dir_entry_t))
		return -EINVAL;

	dentry = malloc(size);
	memset(dent, 0, size);
	while (offs < d->inode->size && offs >= 0) {
		ext2_read_internal(d, offs, (void *)dentry, size, &err);

		if (err) {
			ret = err;
			break;
		}

		if (!dentry->name_len)
			break;

		if (size <= dentry->name_len + sizeof(struct dirent)) {
			ret = -EINVAL;
			break;
		}

		dent->d_ino = dentry->inode;
		dent->d_reclen = dentry->rec_len;
		dent->d_namlen = dentry->name_len;
		dent->d_type = dentry->file_type == EXT2_FT_DIR ? 0 : 1;
		memcpy(&(dent->d_name[0]), dentry->name, dentry->name_len);

		if (object_checkFlag(d, EXT2_FL_MOUNTPOINT) && !strcmp(dent->d_name, ".."))
			dent->d_ino = d->mnt.id;
		ret = dent->d_reclen;
		break;
	}
	d->inode->atime = time(NULL);

	free(dentry);
	return ret;
}


static int ext2_open(ext2_fs_info_t *f, id_t *id)
{
	ext2_object_t *o = object_get(f, id);
	if (o != NULL) {
		TRACE("ino %llu ref %u", o->id, o->refs);
		o->inode->atime = time(NULL);
		return EOK;
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
	TRACE("ino %llu ref %u", o->id, o->refs);
	return EOK;
}


int libext2_handler(void *data, msg_t *msg)
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

			if (!err) {
				TRACE("lookup open %llu", msg->o.lookup.id);
				ext2_open(f, &msg->o.lookup.id);
			}
			break;

		case mtRead:
			msg->o.io = ext2_read(f, &msg->object, msg->i.io.offs, msg->o.data, msg->o.size, &err);
			break;

		case mtWrite:
			msg->o.io = ext2_write(f, &msg->object, msg->i.io.offs, msg->i.data, msg->i.size, &err);
			break;

		case mtSetAttr:
			err = ext2_setattr(f, &msg->object, msg->i.attr, msg->i.data, msg->i.size);
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
			TRACE("direct open %llu",msg->object);
			err = ext2_open(f, &msg->object);
			msg->o.open = msg->object;
			break;

		case mtClose:
			TRACE("direct close %llu", msg->object);
			err = ext2_close(f, &msg->object);
			break;
	}

	return err;
}


int libext2_unmount(void *fsData)
{
	return 0;
}
