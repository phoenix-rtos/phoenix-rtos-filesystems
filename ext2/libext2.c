/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Library
 *
 * Copyright 2019, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/threads.h>

#include "ext2.h"
#include "libext2.h"


static int libext2_create(void *info, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	ext2_t *fs = (ext2_t *)info;
	ext2_obj_t *obj;
	oid_t devOther;
	int ret;
	oid->port = fs->port;

	switch (type) {
		case otDir:
			if (!S_ISDIR(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFDIR;
			}
			break;

		case otFile:
			if (!S_ISREG(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFREG;
			}
			break;

		case otDev:
			if (!EXT2_ISDEV(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFCHR;
			}
			break;

		case otSymlink:
			if (!S_ISLNK(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFLNK;
			}
			break;
	}

	size_t namelen = strlen(name);

	if (ext2_lookup(fs, dir->id, name, namelen, oid, &devOther) > 0) {
		if ((obj = ext2_obj_get(fs, oid->id)) == NULL) {
			return -EINVAL;
		}

		mutexLock(obj->lock);

		if (EXT2_ISDEV(obj->inode->mode) && !EXT2_IS_MOUNTPOINT(obj)) {
			/* This can happen if we have a device file stored in filesystem
			 * but no device oid is associated with it at the moment
			 */
			if (EXT2_ISDEV(mode)) {
				memcpy(&obj->dev, dev, sizeof(oid_t));
				obj->inode->mode = mode;
				obj->flags |= OFLAG_DIRTY | OFLAG_MOUNTPOINT;
				oid->id = obj->id;
				ret = _ext2_obj_sync(fs, obj);

				mutexUnlock(obj->lock);
				ext2_obj_put(fs, obj);
				return ret;
			}
			else {
				mutexUnlock(obj->lock);
				ext2_obj_put(fs, obj);

				if (ext2_unlink(fs, dir->id, name, namelen) < 0) {
					return -EEXIST;
				}
			}
		}
		else {
			mutexUnlock(obj->lock);
			ext2_obj_put(fs, obj);
			return -EEXIST;
		}
	}

	ret = ext2_create(fs, dir->id, name, namelen, dev, mode, &oid->id);

	if (ret >= 0 && type == otSymlink) {
		const char *target = name + namelen + 1;
		size_t targetlen = strlen(target);
		int retWrite;

		/* not writing trailing '\0', readlink() does not append it */
		retWrite = ext2_write(fs, oid->id, 0, target, targetlen);
		if (retWrite < 0) {
			ret = retWrite;
			ext2_destroy(fs, oid->id);
			oid->id = 0;
		}
	}

	return ret;
}


static int libext2_open(void *info, oid_t *oid)
{
	return ext2_open((ext2_t *)info, oid->id);
}


static int libext2_close(void *info, oid_t *oid)
{
	return ext2_close((ext2_t *)info, oid->id);
}


static ssize_t libext2_read(void *info, oid_t *oid, offs_t offs, void *data, size_t len)
{
	return ext2_read((ext2_t *)info, oid->id, offs, data, len);
}


static ssize_t libext2_write(void *info, oid_t *oid, offs_t offs, const void *data, size_t len)
{
	return ext2_write((ext2_t *)info, oid->id, offs, data, len);
}


static int libext2_setattr(void *info, oid_t *oid, int type, long long attr, void *data, size_t len)
{
	return ext2_setattr((ext2_t *)info, oid->id, type, attr, data, len);
}


static int libext2_getattr(void *info, oid_t *oid, int type, long long *attr)
{
	return ext2_getattr((ext2_t *)info, oid->id, type, attr);
}


static int libext2_truncate(void *info, oid_t *oid, size_t size)
{
	return ext2_truncate((ext2_t *)info, oid->id, size);
}


static int libext2_destroy(void *info, oid_t *oid)
{
	return ext2_destroy((ext2_t *)info, oid->id);
}


static int libext2_lookup(void *info, oid_t *oid, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	return ext2_lookup((ext2_t *)info, oid->id, name, strlen(name), res, dev);
}


static int libext2_link(void *info, oid_t *dir, const char *name, oid_t *oid)
{
	return ext2_link((ext2_t *)info, dir->id, name, strlen(name), oid->id);
}


static int libext2_unlink(void *info, oid_t *oid, const char *name)
{
	return ext2_unlink((ext2_t *)info, oid->id, name, strlen(name));
}


static int libext2_readdir(void *info, oid_t *oid, offs_t offs, struct dirent *dent, size_t size)
{
	return ext2_read((ext2_t *)info, oid->id, offs, (char *)dent, size);
}


static int libext2_statfs(void *info, void *buf, size_t len)
{
	return ext2_statfs((ext2_t *)info, buf, len);
}


int libext2_handler(void *fdata, msg_t *msg)
{
	switch (msg->type) {
		case mtCreate:
			msg->o.create.err = libext2_create(fdata, &msg->i.create.dir, msg->i.data, &msg->o.create.oid, msg->i.create.mode, msg->i.create.type, &msg->i.create.dev);
			break;

		case mtDestroy:
			msg->o.io.err = libext2_destroy(fdata, &msg->i.destroy.oid);
			break;

		case mtLookup:
			msg->o.lookup.err = libext2_lookup(fdata, &msg->i.lookup.dir, msg->i.data, &msg->o.lookup.fil, &msg->o.lookup.dev, msg->o.data, msg->o.size);
			break;

		case mtOpen:
			msg->o.io.err = libext2_open(fdata, &msg->i.openclose.oid);
			break;

		case mtClose:
			msg->o.io.err = libext2_close(fdata, &msg->i.openclose.oid);
			break;

		case mtRead:
			msg->o.io.err = libext2_read(fdata, &msg->i.io.oid, msg->i.io.offs, msg->o.data, msg->o.size);
			break;

		case mtReaddir:
			msg->o.io.err = libext2_readdir(fdata, &msg->i.readdir.dir, msg->i.readdir.offs, msg->o.data, msg->o.size);
			break;

		case mtWrite:
			msg->o.io.err = libext2_write(fdata, &msg->i.io.oid, msg->i.io.offs, msg->i.data, msg->i.size);
			break;

		case mtTruncate:
			msg->o.io.err = libext2_truncate(fdata, &msg->i.io.oid, msg->i.io.len);
			break;

		case mtDevCtl:
			msg->o.io.err = -EINVAL;
			break;

		case mtGetAttr:
			msg->o.attr.err = libext2_getattr(fdata, &msg->i.attr.oid, msg->i.attr.type, &msg->o.attr.val);
			break;

		case mtSetAttr:
			msg->o.attr.err = libext2_setattr(fdata, &msg->i.attr.oid, msg->i.attr.type, msg->i.attr.val, msg->i.data, msg->i.size);
			break;

		case mtLink:
			msg->o.io.err = libext2_link(fdata, &msg->i.ln.dir, msg->i.data, &msg->i.ln.oid);
			break;

		case mtUnlink:
			msg->o.io.err = libext2_unlink(fdata, &msg->i.ln.dir, msg->i.data);
			break;

		case mtStat:
			msg->o.io.err = libext2_statfs(fdata, msg->o.data, msg->o.size);
			break;

		default:
			break;
	}

	return EOK;
}


int libext2_unmount(void *fdata)
{
	ext2_t *fs = (ext2_t *)fdata;

	ext2_objs_destroy(fs);
	ext2_gdt_destroy(fs);
	ext2_sb_destroy(fs);
	free(fs);

	return EOK;
}


int libext2_mount(oid_t *oid, unsigned int sectorsz, dev_read read, dev_write write, void **fdata)
{
	ext2_t *fs;
	int err;

	if ((*fdata = fs = (ext2_t *)malloc(sizeof(ext2_t))) == NULL)
		return -ENOMEM;

	fs->sectorsz = sectorsz;
	fs->strg = NULL;
	fs->legacy.devId = oid->id;
	fs->legacy.read = read;
	fs->legacy.write = write;
	fs->port = oid->port;

	if ((err = ext2_sb_init(fs)) < 0) {
		free(fs);
		return err;
	}

	if ((err = ext2_gdt_init(fs)) < 0) {
		ext2_sb_destroy(fs);
		free(fs);
		return err;
	}

	if ((err = ext2_objs_init(fs)) < 0) {
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
		free(fs);
		return err;
	}

	if ((fs->root = ext2_obj_get(fs, ROOT_INO)) == NULL) {
		ext2_objs_destroy(fs);
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
		free(fs);
		return -ENOENT;
	}

	return ROOT_INO;
}


const static storage_fsops_t fsOps = {
	.open = libext2_open,
	.close = libext2_close,
	.read = libext2_read,
	.write = libext2_write,
	.setattr = libext2_setattr,
	.getattr = libext2_getattr,
	.truncate = libext2_truncate,
	.devctl = NULL,
	.create = libext2_create,
	.destroy = libext2_destroy,
	.lookup = libext2_lookup,
	.link = libext2_link,
	.unlink = libext2_unlink,
	.readdir = libext2_readdir,
	.statfs = libext2_statfs,
	.sync = NULL
};


int libext2_storage_umount(storage_fs_t *strg_fs)
{
	ext2_t *fs = (ext2_t *)strg_fs->info;
	ext2_objs_destroy(fs);
	ext2_gdt_destroy(fs);
	ext2_sb_destroy(fs);
	free(fs);

	return EOK;
}


int libext2_storage_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root)
{
	ext2_t *info;
	int err;

	if (strg == NULL ||
		strg->dev == NULL ||
		strg->dev->blk == NULL ||
		strg->dev->blk->ops == NULL ||
		strg->dev->blk->ops->read == NULL ||
		strg->dev->blk->ops->write == NULL) {
		return -ENOSYS;
	}

	info = (ext2_t *)malloc(sizeof(ext2_t));
	if (info == NULL) {
		return -ENOMEM;
	}

	/* TODO: this needs to be configurable to be compatible with pc-ata */
	info->sectorsz = 512;
	info->strg = strg;
	info->legacy.devId = 0;
	info->legacy.read = NULL;
	info->legacy.write = NULL;
	info->port = root->port;
	root->id = ROOT_INO;

	fs->info = info;
	fs->ops = &fsOps;

	if ((err = ext2_sb_init(info)) < 0) {
		free(info);
		return err;
	}

	if ((err = ext2_gdt_init(info)) < 0) {
		ext2_sb_destroy(info);
		free(info);
		return err;
	}

	if ((err = ext2_objs_init(info)) < 0) {
		ext2_gdt_destroy(info);
		ext2_sb_destroy(info);
		free(info);
		return err;
	}

	if ((info->root = ext2_obj_get(info, ROOT_INO)) == NULL) {
		ext2_objs_destroy(info);
		ext2_gdt_destroy(info);
		ext2_sb_destroy(info);
		free(info);
		return -ENOENT;
	}

	return EOK;
}
