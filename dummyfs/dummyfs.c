/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018, 2021 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk, Kamil Amanowicz, Maciej Purski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h> /* to set mode for / */
#include <sys/list.h>
#include <sys/mount.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <poll.h>
#include "dummyfs.h"
#include "dir.h"
#include "file.h"
#include "object.h"
#include "dev.h"


int dummyfs_incsz(dummyfs_t *ctx, int size)
{
	if (ctx->size + size > DUMMYFS_SIZE_MAX)
		return -ENOMEM;
	ctx->size += size;
	return EOK;
}

void dummyfs_decsz(dummyfs_t *ctx, int size)
{
	ctx->size -= size;
}


static inline int dummyfs_device(dummyfs_t *ctx, oid_t *oid)
{
	return oid->port != ctx->port;
}


static inline dummyfs_object_t *dummyfs_get(dummyfs_t *ctx, oid_t *oid)
{
	return dummyfs_device(ctx, oid) ? dev_find(ctx, oid, 0) : object_get(ctx, oid->id);
}


int dummyfs_lookup(dummyfs_t *ctx, oid_t *dir, const char *name, oid_t *res, oid_t *dev)
{
	dummyfs_object_t *o, *d;
	int len = 0;
	int err = -ENOENT;

	if (dir == NULL)
		d = object_get(ctx, 0);
	else if (dummyfs_device(ctx, dir))
		return -EINVAL;
	else if ((d = object_get(ctx, dir->id)) == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(ctx, d);
		return -ENOTDIR;
	}

	object_lock(ctx, d);
	while (name[len] != '\0') {
		while (name[len] == '/')
			len++;

		err = dir_find(d, name + len, res);

		if (err <= 0)
			break;

		len += err;
		object_unlock(ctx, d);
		object_put(ctx, d);

		if (dummyfs_device(ctx, res))
			break;

		d = object_get(ctx, res->id);
		object_lock(ctx, d);
	}

	if (err < 0) {
		object_unlock(ctx, d);
		object_put(ctx, d);
		return err;
	}

	o = dummyfs_get(ctx, res);

	if (S_ISCHR(d->mode) || S_ISBLK(d->mode) || S_ISFIFO(d->mode))
		memcpy(dev, &o->dev, sizeof(oid_t));
	else
		memcpy(dev, res, sizeof(oid_t));

	object_put(ctx, o);
	object_unlock(ctx, d);
	object_put(ctx, d);

	return len;
}


int dummyfs_setattr(dummyfs_t *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size)
{
	dummyfs_object_t *o;
	int ret = EOK;

	if ((o = dummyfs_get(ctx, oid)) == NULL)
		return -ENOENT;

	object_lock(ctx, o);
	switch (type) {
		case (atUid):
			o->uid = attr;
			break;

		case (atGid):
			o->gid = attr;
			break;

		case (atMode):
			o->mode = attr;
			break;

		case (atSize):
			object_unlock(ctx, o);
			ret = dummyfs_truncate(ctx, oid, attr);
			object_lock(ctx, o);
			break;

		case (atPort):
			ret = -EINVAL;
			break;

		case (atDev):
			/* TODO: add mouting capabilities */
			ret = -EINVAL;
			break;
	}

	o->mtime = time(NULL);

	object_unlock(ctx, o);
	object_put(ctx, o);

	return ret;
}


int dummyfs_getattr(dummyfs_t *ctx, oid_t *oid, int type, long long *attr)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(ctx, oid)) == NULL)
		return -ENOENT;

	object_lock(ctx, o);
	switch (type) {

		case (atUid):
			*attr = o->uid;
			break;

		case (atGid):
			*attr = o->gid;
			break;

		case (atMode):
			*attr = o->mode;
			break;

		case (atSize):
			*attr = o->size;
			break;

		case (atType):
			if (S_ISDIR(o->mode))
				*attr = otDir;
			else if (S_ISREG(o->mode))
				*attr = otFile;
			else if (S_ISCHR(o->mode) || S_ISBLK(o->mode) || S_ISFIFO(o->mode))
				*attr = otDev;
			else if (S_ISLNK(o->mode))
				*attr = otSymlink;
			else
				*attr = otUnknown;
			break;

		case (atPort):
			*attr = o->oid.port;
			break;

		case (atCTime):
			*attr = o->ctime;
			break;

		case (atMTime):
			*attr = o->mtime;
			break;

		case (atATime):
			*attr = o->atime;
			break;

		case (atLinks):
			*attr = o->nlink;
			break;

		case (atPollStatus):
			// trivial implementation: assume read/write is always possible
			*attr = POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM;
			break;
	}

	object_unlock(ctx, o);
	object_put(ctx, o);

	return EOK;
}

// allow overriding files by link() to support naive rename() implementation
#define LINK_ALLOW_OVERRIDE 1

int dummyfs_link(dummyfs_t *ctx, oid_t *dir, const char *name, oid_t *oid)
{
	dummyfs_object_t *d, *o, *victim_o = NULL;
	int ret;
	oid_t victim_oid;

	if (name == NULL)
		return -EINVAL;

	if (dummyfs_device(ctx, dir))
		return -EINVAL;

	if ((d = object_get(ctx, dir->id)) == NULL)
		return -ENOENT;

	if ((o = dummyfs_get(ctx, oid)) == NULL) {
		object_put(ctx, d);
		return -ENOENT;
	}

	if (!S_ISDIR(d->mode)) {
		object_put(ctx, o);
		object_put(ctx, d);
		return -EEXIST;
	}

	if (S_ISDIR(o->mode) && o->nlink != 0) {
		object_put(ctx, o);
		object_put(ctx, d);
		return -EINVAL;
	}

	o->nlink++;

	if (S_ISDIR(o->mode)) {
		object_lock(ctx, o);
		dir_add(ctx, o, ".", S_IFDIR | 0666, oid);
		dir_add(ctx, o, "..", S_IFDIR | 0666, dir);
		o->nlink++;
		object_unlock(ctx, o);
		object_lock(ctx, d);
		d->nlink++;
		object_unlock(ctx, d);
	}

#ifdef LINK_ALLOW_OVERRIDE
	if (dir_find(d, name, &victim_oid) > 0) {
		victim_o = object_get(ctx, victim_oid.id);
		if (S_ISDIR(victim_o->mode) // explicitly disallow overwriting directories
				|| victim_oid.id == oid->id) { // linking to self
			object_put(ctx, victim_o);
			victim_o = NULL;
		}
		else {
			// object_lock(victim_o); //FIXME: per-object locking
		}
	}
#endif

	object_lock(ctx, d);
	if (!victim_o) {
		ret = dir_add(ctx, d, name, o->mode, oid);
	}
	else {
		ret = dir_replace(d, name, oid);
		victim_o->nlink--;
		// object_unlock(ctx, victim_o); //FIXME: per-object locking
	}

	if (ret != EOK) {
		object_unlock(ctx, d);
		object_lock(ctx, o);
		o->nlink--;
		if (S_ISDIR(o->mode))
			o->nlink--;
		object_unlock(ctx, o);
	}

	d->mtime = d->atime = o->mtime = time(NULL);

	object_unlock(ctx, d);
	object_put(ctx, o);
	object_put(ctx, d);
	object_put(ctx, victim_o);

	return ret;
}


int dummyfs_unlink(dummyfs_t *ctx, oid_t *dir, const char *name)
{
	oid_t oid;
	dummyfs_object_t *o, *d;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return -EINVAL;

	if (dummyfs_device(ctx, dir))
		return -EINVAL;

	d = object_get(ctx, dir->id);

	if (d == NULL)
		return -EINVAL;

	object_lock(ctx, d);

	if (dir_find(d, name, &oid) < 0) {
		object_unlock(ctx, d);
		object_put(ctx, d);
		return -ENOENT;
	}

	if (oid.id == 0) {
		object_unlock(ctx, d);
		object_put(ctx, d);
		return -EINVAL;
	}

	o = dummyfs_get(ctx, &oid);

	if (o == NULL) {
		object_unlock(ctx, d);
		object_put(ctx, d);
		return -ENOENT;
	}

	if (S_ISDIR(o->mode) && dir_empty(ctx, o) != EOK) {
		object_unlock(ctx, d);
		object_put(ctx, d);
		object_put(ctx, o);
		return -ENOTEMPTY;
	}

	ret = dir_remove(ctx, d, name);

	if (ret == EOK && S_ISDIR(o->mode))
		d->nlink--;

	d->mtime = d->atime = o->mtime = time(NULL);

	object_unlock(ctx, d);
	object_put(ctx, d);

	if (ret == EOK) {
		object_lock(ctx, o);
		o->nlink--;
		if (S_ISDIR(o->mode))
			o->nlink--;
		object_unlock(ctx, o);
	}
	object_put(ctx, o);

	return ret;
}


int dummyfs_create(dummyfs_t *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	dummyfs_object_t *o;
	int ret;

	switch (type) {
		case otDir:
			mode |= S_IFDIR;
			break;

		case otFile:
			mode |= S_IFREG;
			break;

		case otDev:
			if (!(S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))) {
				mode &= 0x1ff;
				mode |= S_IFCHR;
			}
			break;

		case otSymlink:
			mode |= S_IFLNK;
			break;
	}

	if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))
		o = dev_find(ctx, dev, 1);
	else
		o = object_create(ctx);

	if (o == NULL)
		return -ENOMEM;

	object_lock(ctx, o);
	o->oid.port = ctx->port;
	o->mode = mode;
	o->atime = o->mtime = o->ctime = time(NULL);

	if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))
		memcpy(oid, dev, sizeof(oid_t));
	else
		memcpy(oid, &o->oid, sizeof(oid_t));

	object_unlock(ctx, o);

	if ((ret = dummyfs_link(ctx, dir, name, &o->oid)) != EOK) {
		object_put(ctx, o);
		return ret;
	}

	if (S_ISLNK(mode)) {
		const char* path = name + strlen(name) + 1;
		object_lock(ctx, o);
		/* TODO: remove symlink if write failed */
		dummyfs_write_internal(ctx, o, 0, path, strlen(path));
		object_unlock(ctx, o);
	}

	object_put(ctx, o);
	return EOK;
}


int dummyfs_destroy(dummyfs_t *ctx, oid_t *oid)
{
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get_unlocked(ctx, oid->id);

	if (o == NULL)
		return -ENOENT;

	if ((ret = object_remove(ctx, o)) == EOK) {
		if (S_ISREG(o->mode)) {
			object_lock(ctx, o);
			dummyfs_truncate_internal(ctx, o, 0);
			object_unlock(ctx, o);
		}
		else if (S_ISDIR(o->mode))
			dir_destroy(ctx, o);
		else if (S_ISCHR(o->mode) || S_ISBLK(o->mode) || S_ISFIFO(o->mode))
			dev_destroy(ctx, &o->dev);

		else if (o->mode == 0xaBadBabe) {
#ifndef NOMMU
			munmap((void *)((uintptr_t)o->chunks->data & ~0xfff), (o->size + 0xfff) & ~0xfff);
#endif
			free(o->chunks);
		}
		dummyfs_decsz(ctx, sizeof(dummyfs_object_t));
		free(o);
	}

	return ret;
}


int dummyfs_readdir(dummyfs_t *ctx, oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	dummyfs_object_t *d;
	dummyfs_dirent_t *ei;
	offs_t diroffs = 0;
	int ret = -ENOENT;

	if (dummyfs_device(ctx, dir))
		return -EINVAL;

	d = object_get(ctx, dir->id);

	if (d == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(ctx, d);
		return -EINVAL;
	}

	object_lock(ctx, d);

	if ((ei = d->entries) == NULL) {
		object_unlock(ctx, d);
		object_put(ctx, d);
		return -EINVAL;
	}
	dent->d_reclen = 0;
	do {
		if (diroffs >= offs) {
			if ((sizeof(struct dirent) + ei->len + 1) > size) {
				object_unlock(ctx, d);
				object_put(ctx, d);
				return 	-EINVAL;
			}
			if (ei->deleted) {
				ei = ei->next;
				dent->d_reclen++;
				continue;
			}

			dent->d_ino = ei->oid.id;
			dent->d_reclen++;
			dent->d_namlen = ei->len;
			dent->d_type = ei->type;
			strcpy(dent->d_name, ei->name);

			object_unlock(ctx, d);
			object_put(ctx, d);
			return 	EOK;
		}
		diroffs++;
		ei = ei->next;
	} while (ei != d->entries);

	d->atime = time(NULL);

	object_unlock(ctx, d);
	object_put(ctx, d);

	return ret;
}


int dummyfs_open(dummyfs_t *ctx, oid_t *oid)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(ctx, oid)) == NULL)
		return -ENOENT;

	object_lock(ctx, o);
	o->atime = time(NULL);

	object_unlock(ctx, o);
	return EOK;
}


int dummyfs_close(dummyfs_t *ctx, oid_t *oid)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(ctx, oid)) == NULL)
		return -ENOENT;

	object_lock(ctx, o);
	o->atime = time(NULL);

	object_unlock(ctx, o);
	object_put(ctx, o);
	object_put(ctx, o);
	return EOK;
}


int dummyfs_truncate(dummyfs_t *ctx, oid_t *oid, size_t size)
{
	dummyfs_object_t *o;
	int ret;

	o = object_get(ctx, oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode)) {
		object_put(ctx, o);
		return -EACCES;
	}

	if (o->size == size) {
		object_put(ctx, o);
		return EOK;
	}

	object_lock(ctx, o);

	ret = dummyfs_truncate_internal(ctx, o, size);

	object_unlock(ctx, o);
	object_put(ctx, o);

	return ret;
}




int dummyfs_read(dummyfs_t *ctx, oid_t *oid, offs_t offs, char *buff, size_t len)
{
	int ret = EOK;
	int readsz;
	int readoffs;
	dummyfs_chunk_t *chunk;
	dummyfs_object_t *o;

	o = object_get(ctx, oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode) && !S_ISLNK(o->mode) && o->mode != 0xaBadBabe)
		ret = -EINVAL;

	if (buff == NULL)
		ret = -EINVAL;

	if (o->size <= offs) {
		object_put(ctx, o);
		return 0;
	}

	if (ret != EOK) {
		object_put(ctx, o);
		return ret;
	}

	if (len == 0) {
		object_put(ctx, o);
		return EOK;
	}

	object_lock(ctx, o);
	for (chunk = o->chunks; chunk->next != o->chunks; chunk = chunk->next) {
		if (chunk->offs + chunk->size > offs) {
			break;
		}
	}

	do {
		readoffs = offs - chunk->offs;
		readsz = len > chunk->size - readoffs ? chunk->size - readoffs : len;
		if (chunk->used)
			memcpy(buff, chunk->data + readoffs, readsz);
		else
			memset(buff, 0, readsz);

		len  -= readsz;
		buff += readsz;
		offs += readsz;
		ret  += readsz;

		chunk = chunk->next;

	} while (len && chunk != o->chunks);

	o->atime = time(NULL);
	object_unlock(ctx, o);
	object_put(ctx, o);

	return ret;
}


int dummyfs_write(dummyfs_t *ctx, oid_t *oid, offs_t offs, const char *buff, size_t len)
{
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get(ctx, oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode))
		ret = -EINVAL;

	if (buff == NULL)
		ret = -EINVAL;

	if (ret != EOK) {
		object_put(ctx, o);
		return ret;
	}

	object_lock(ctx, o);

	ret = dummyfs_write_internal(ctx, o, offs, buff, len);

	object_unlock(ctx, o);
	object_put(ctx, o);

	return ret;
}
