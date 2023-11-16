/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Filesystem operations
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/threads.h>

#include "dir.h"
#include "ext2.h"
#include "file.h"


int ext2_create(ext2_t *fs, id_t id, const char *name, size_t len, oid_t *dev, uint16_t mode, id_t *res)
{
	ext2_obj_t *obj;
	int err;

	if ((err = ext2_obj_create(fs, (uint32_t)id, NULL, mode, &obj)) < 0)
		return err;

	if (ext2_link(fs, id, name, len, obj->id) < 0)
		return ext2_obj_destroy(fs, obj);

	if (EXT2_ISDEV(obj->inode->mode)) {
		memcpy(&obj->dev, dev, sizeof(oid_t));
		obj->flags |= OFLAG_MOUNTPOINT;
	}

	*res = obj->id;
	ext2_obj_put(fs, obj);

	return EOK;
}


int ext2_destroy(ext2_t *fs, id_t id)
{
	ext2_obj_t *obj;
	int err;

	if ((obj = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	do {
		if ((err = ext2_obj_sync(fs, obj)) < 0)
			break;

		if ((err = ext2_obj_truncate(fs, obj, 0)) < 0)
			break;

		return ext2_obj_destroy(fs, obj);
	} while (0);

	ext2_obj_put(fs, obj);

	return err;
}


int ext2_lookup(ext2_t *fs, id_t id, const char *name, size_t len, oid_t *res, oid_t *dev)
{
	ext2_obj_t *dir, *obj = NULL;
	size_t i, j;
	int err;

	res->port = fs->port;
	if ((len == 0) || (name == NULL)) {
		return -EINVAL;
	}

	if ((dir = ext2_obj_get(fs, id)) == NULL) {
		return -ENOENT;
	}

	mutexLock(dir->lock);
	for (i = 0, j = 0; i < len; i = j + 1, dir = obj) {
		while ((i < len) && (name[i] == '/')) {
			i++;
		}

		j = i + 1;

		while ((j < len) && (name[j] != '/')) {
			j++;
		}

		do {
			if (i >= len) {
				err = -ENOENT;
				break;
			}

			if (!S_ISDIR(dir->inode->mode)) {
				err = -ENOTDIR;
				break;
			}

			err = _ext2_dir_search(fs, dir, name + i, j - i, &res->id);
			if (err < 0) {
				break;
			}

			obj = ext2_obj_get(fs, res->id);
			if (obj == NULL) {
				ext2_unlink(fs, dir->id, name + i, j - i);
				err = -ENOENT;
				break;
			}
		} while (0);

		mutexUnlock(dir->lock);
		ext2_obj_put(fs, dir);

		if (err < 0) {
			return err;
		}

		mutexLock(obj->lock);
		if (EXT2_IS_MOUNTPOINT(obj) && S_ISDIR(obj->inode->mode)) {
			break;
		}
	}

	*dev = EXT2_IS_MOUNTPOINT(obj) ? obj->dev : *res;

	mutexUnlock(obj->lock);
	ext2_obj_put(fs, obj);

	return j;
}


int ext2_open(ext2_t *fs, id_t id)
{
	ext2_obj_t *obj;

	if ((obj = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	mutexLock(obj->lock);

	obj->inode->atime = time(NULL);

	mutexUnlock(obj->lock);

	return EOK;
}


int ext2_close(ext2_t *fs, id_t id)
{
	ext2_obj_t *obj;
	int err;

	if ((obj = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	if ((err = ext2_obj_sync(fs, obj)) < 0)
		return err;

	ext2_obj_put(fs, obj);
	ext2_obj_put(fs, obj);

	return EOK;
}


ssize_t ext2_read(ext2_t *fs, id_t id, offs_t offs, char *buff, size_t len)
{
	ext2_obj_t *obj;
	ssize_t ret;

	if ((obj = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	mutexLock(obj->lock);

	if (S_ISDIR(obj->inode->mode)) {
		if (EXT2_IS_MOUNTPOINT(obj)) {
			ret = -EINVAL;
		}
		else {
			ret = _ext2_dir_read(fs, obj, offs, (struct dirent *)buff, len);
		}
	}
	else if (EXT2_ISDEV(obj->inode->mode)) {
		ret = -EINVAL;
	}
	else {
		ret = _ext2_file_read(fs, obj, offs, buff, len);
	}

	mutexUnlock(obj->lock);
	ext2_obj_put(fs, obj);

	return ret;
}


ssize_t ext2_write(ext2_t *fs, id_t id, offs_t offs, const char *buff, size_t len)
{
	ext2_obj_t *obj;
	ssize_t ret;

	if ((obj = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	mutexLock(obj->lock);

	if (S_ISDIR(obj->inode->mode) || EXT2_ISDEV(obj->inode->mode)) {
		ret = -EINVAL;
	}
	else {
		ret = _ext2_file_write(fs, obj, offs, buff, len);
	}

	mutexUnlock(obj->lock);
	ext2_obj_put(fs, obj);

	return ret;
}


int ext2_truncate(ext2_t *fs, id_t id, size_t size)
{
	ext2_obj_t *obj;
	int err;

	if ((obj = ext2_obj_get(fs, id)) == NULL) {
		return -EINVAL;
	}

	do {
		mutexLock(obj->lock);

		if (!S_ISREG(obj->inode->mode)) {
			if (S_ISDIR(obj->inode->mode)) {
				err = -EISDIR;
			}
			else {
				err = -EINVAL;
			}
			mutexUnlock(obj->lock);
			break;
		}

		mutexUnlock(obj->lock);

		if ((err = ext2_obj_truncate(fs, obj, size)) < 0)
			break;

		if ((err = ext2_sb_sync(fs)) < 0)
			break;
	} while (0);

	ext2_obj_put(fs, obj);

	return err;
}


int ext2_getattr(ext2_t *fs, id_t id, int type, long long *attr)
{
	int ret = EOK;
	ext2_obj_t *obj = ext2_obj_get(fs, id);

	if (obj == NULL) {
		return -EINVAL;
	}

	mutexLock(obj->lock);

	switch(type) {
		case atMode:
			*attr = obj->inode->mode;
			break;

		case atUid:
			*attr = obj->inode->uid;
			break;

		case atGid:
			*attr = obj->inode->gid;
			break;

		case atSize:
			*attr = obj->inode->size;
			break;

		case atBlocks:
			*attr = obj->inode->blocks;
			break;

		case atIOBlock:
			/* TODO: determine optimal I/O block size */
			/* fs->blocksz seems reasonable for now */
			*attr = fs->blocksz;
			break;

		case atType:
			if (S_ISDIR(obj->inode->mode)) {
				*attr = otDir;
			}
			else if (S_ISREG(obj->inode->mode)) {
				*attr = otFile;
			}
			else if (EXT2_ISDEV(obj->inode->mode)) {
				*attr = otDev;
			}
			else if (S_ISLNK(obj->inode->mode)) {
				*attr = otSymlink;
			}
			else {
				*attr = otUnknown;
			}
			break;

		case atCTime:
			*attr = obj->inode->ctime;
			break;

		case atATime:
			*attr = obj->inode->atime;
			break;

		case atMTime:
			*attr = obj->inode->mtime;
			break;

		case atLinks:
			*attr = obj->inode->links;
			break;

		case atPollStatus:
			*attr = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
			break;

		default:
			ret = -EINVAL;
			break;
	}

	mutexUnlock(obj->lock);
	ext2_obj_put(fs, obj);

	return ret;
}


int ext2_setattr(ext2_t *fs, id_t id, int type, long long attr, void *data, size_t len)
{
	ext2_obj_t *obj;
	int err = EOK;

	if ((obj = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	mutexLock(obj->lock);

	switch(type) {
	case atMode:
		obj->inode->mode = (obj->inode->mode & ~ALLPERMS) | (attr & ALLPERMS);
		break;

	case atUid:
		obj->inode->uid = attr;
		break;

	case atGid:
		obj->inode->gid = attr;
		break;

	case atSize:
		if ((err = _ext2_file_truncate(fs, obj, attr)) < 0)
			break;

		if ((err = _ext2_obj_sync(fs, obj)) < 0)
			break;

		if ((err = ext2_sb_sync(fs)) < 0)
			break;
		break;

	case atMTime:
		obj->inode->mtime = attr;
		break;

	case atATime:
		obj->inode->atime = attr;
		break;

	case atDev:
		if (data != NULL && len == sizeof(oid_t)) {
			oid_t dev;
			memcpy(&dev, data, len);
			if ((dev.port == fs->port) && (dev.id == id)) {
				obj->flags &= ~OFLAG_MOUNTPOINT;
			}
			else {
				obj->dev = dev;
				obj->flags |= OFLAG_MOUNTPOINT;
			}
		}
		else {
			err = -EINVAL;
		}
		break;


	default:
		/* unknown / invalid attribute to set */
		err = -EINVAL;
		break;
	}

	if (err == EOK) {
		if (type != atMTime && type != atATime) {
			obj->inode->mtime = obj->inode->atime = time(NULL);
		}

		obj->flags |= OFLAG_DIRTY;
		err = _ext2_obj_sync(fs, obj);
	}

	mutexUnlock(obj->lock);
	ext2_obj_put(fs, obj);

	return err;
}


int ext2_link(ext2_t *fs, id_t id, const char *name, size_t len, id_t lid)
{
	ext2_obj_t *dir, *obj;
	id_t res;
	int err;

	if (!len || (name == NULL) || (id == lid))
		return -EINVAL;

	if ((dir = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	if ((obj = ext2_obj_get(fs, lid)) == NULL) {
		ext2_obj_put(fs, dir);
		return -EINVAL;
	}

	mutexLock(dir->lock);
	mutexLock(obj->lock);

	do {
		if (!S_ISDIR(dir->inode->mode)) {
			err = -ENOTDIR;
			break;
		}

		if (!_ext2_dir_search(fs, dir, name, len, &res)) {
			err = -EEXIST;
			break;
		}

		if (S_ISDIR(obj->inode->mode) && obj->inode->links) {
			err = -EMLINK;
			break;
		}

		if ((err = _ext2_dir_add(fs, dir, name, len, obj->inode->mode, (uint32_t)lid)) < 0)
			break;

		obj->inode->links++;
		obj->inode->uid = 0;
		obj->inode->gid = 0;
		obj->inode->mtime = obj->inode->atime = time(NULL);
		obj->flags |= OFLAG_DIRTY;

		if (S_ISDIR(obj->inode->mode)) {
			if ((err = _ext2_dir_add(fs, obj, ".", 1, S_IFDIR, (uint32_t)lid)) < 0)
				break;

			obj->inode->links++;

			if ((err = _ext2_dir_add(fs, obj, "..", 2, S_IFDIR, (uint32_t)id)) < 0)
				break;

			dir->inode->links++;
			dir->flags |= OFLAG_DIRTY;

			if ((err = _ext2_obj_sync(fs, dir)) < 0)
				break;
		}

		if ((err = _ext2_obj_sync(fs, obj)) < 0)
			break;
	} while (0);

	mutexUnlock(obj->lock);
	mutexUnlock(dir->lock);
	ext2_obj_put(fs, obj);
	ext2_obj_put(fs, dir);

	return err;
}


int ext2_unlink(ext2_t *fs, id_t id, const char *name, size_t len)
{
	ext2_obj_t *dir, *obj;
	id_t res;
	int err;

	if (!len || (name == NULL))
		return -EINVAL;

	if ((dir = ext2_obj_get(fs, id)) == NULL)
		return -EINVAL;

	mutexLock(dir->lock);

	do {
		if (!S_ISDIR(dir->inode->mode)) {
			err = -ENOTDIR;
			break;
		}

		if ((err = _ext2_dir_search(fs, dir, name, len, &res)) < 0)
			break;

		if ((obj = ext2_obj_get(fs, res)) == NULL) {
			if (!(err = _ext2_dir_remove(fs, dir, name, len)))
				err = -ENOENT;
			break;
		}

		mutexLock(obj->lock);

		do {
			if (S_ISDIR(obj->inode->mode) && (EXT2_IS_MOUNTPOINT(obj) || !_ext2_dir_empty(fs, obj))) {
				err = -ENOTEMPTY;
				break;
			}

			if ((err = _ext2_dir_remove(fs, dir, name, len)) < 0)
				break;

			obj->inode->links--;
			if (S_ISDIR(obj->inode->mode)) {
				dir->inode->links--;
				obj->inode->links--;
				break;
			}

			obj->inode->mtime = obj->inode->atime = time(NULL);
		} while (0);

		mutexUnlock(obj->lock);
		ext2_obj_put(fs, obj);
	} while (0);

	mutexUnlock(dir->lock);
	ext2_obj_put(fs, dir);

	return err;
}


int ext2_statfs(ext2_t *fs, void *buf, size_t len)
{
	ext2_sb_t *sb = fs->sb;
	struct statvfs *st = buf;

	if ((st == NULL) || (len != sizeof(*st))) {
		return -EINVAL;
	}

	/* TODO: superblock access should be protected with a lock */
	st->f_bsize = st->f_frsize = fs->blocksz;
	st->f_blocks = sb->blocks;
	st->f_bfree = sb->freeBlocks;
	st->f_bavail = (sb->freeBlocks > sb->resBlocks) ? sb->freeBlocks - sb->resBlocks : 0;
	st->f_files = sb->inodes;
	st->f_favail = st->f_ffree = sb->freeInodes;
	st->f_fsid = (unsigned long)fs; /* TODO: filesystem ID should be generated at mount time */
	st->f_flag = 0;                 /* TODO: mount options should be saved at mount time */
	st->f_namemax = MAX_NAMELEN;

	return EOK;
}
