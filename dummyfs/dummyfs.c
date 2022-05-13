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
#include <sys/statvfs.h>
#include <sys/list.h>
#include <sys/mount.h>
#include <sys/threads.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <poll.h>

#include "dummyfs.h"
#include "dir.h"
#include "file.h"
#include "object.h"
#include "dev.h"


/* higher two bytes used as magic number */
#define OBJECT_MODE_MEM (0xaBad0000 | S_IFREG | S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)


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


int dummyfs_lookup(void *ctx, oid_t *dir, const char *name, oid_t *res, oid_t *dev)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o, *d;
	int len = 0;
	int err = -ENOENT;

	if (dir == NULL)
		d = object_get(fs, 0);
	else if (dummyfs_device(fs, dir))
		return -EINVAL;
	else if ((d = object_get(fs, dir->id)) == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(fs, d);
		return -ENOTDIR;
	}

	object_lock(fs, d);
	while (name[len] != '\0') {
		while (name[len] == '/')
			len++;

		err = dir_find(d, name + len, res);

		if (err <= 0)
			break;

		len += err;
		object_unlock(fs, d);
		object_put(fs, d);

		if (dummyfs_device(fs, res))
			break;

		d = object_get(fs, res->id);
		object_lock(fs, d);
	}

	if (err < 0) {
		object_unlock(fs, d);
		object_put(fs, d);
		return err;
	}

	o = dummyfs_get(fs, res);

	if (S_ISCHR(d->mode) || S_ISBLK(d->mode) || S_ISFIFO(d->mode))
		memcpy(dev, &o->dev, sizeof(oid_t));
	else
		memcpy(dev, res, sizeof(oid_t));

	object_put(fs, o);
	object_unlock(fs, d);
	object_put(fs, d);

	return len;
}


int dummyfs_setattr(void *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;
	int ret = EOK;

	if ((o = dummyfs_get(fs, oid)) == NULL)
		return -ENOENT;

	object_lock(fs, o);
	switch (type) {
		case (atUid):
			o->uid = attr;
			break;

		case (atGid):
			o->gid = attr;
			break;

		case (atMode):
			o->mode = (o->mode & ~ALLPERMS) | (attr & ALLPERMS);
			break;

		case (atSize):
			object_unlock(fs, o);
			ret = dummyfs_truncate(ctx, oid, attr);
			object_lock(fs, o);
			break;

		case (atPort):
			ret = -EINVAL;
			break;

		case (atDev):
			/* TODO: add mouting capabilities */
			ret = -EINVAL;
			break;

		case (atMTime):
			o->mtime = attr;
			break;

		case (atATime):
			o->atime = attr;
			break;
	}

	/* FIXME: mtime and atime are always set together */
	if (type != atMTime && type != atATime)
		o->mtime = time(NULL);

	object_unlock(fs, o);
	object_put(fs, o);

	return ret;
}


int dummyfs_getattr(void *ctx, oid_t *oid, int type, long long *attr)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;

	if ((o = dummyfs_get(fs, oid)) == NULL)
		return -ENOENT;

	object_lock(fs, o);
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

		case (atBlocks):
			*attr = (o->size + S_BLKSIZE - 1) / S_BLKSIZE;
			break;

		case (atIOBlock):
			/* TODO: determine optimal I/O block size */
			*attr = 1;
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

	object_unlock(fs, o);
	object_put(fs, o);

	return EOK;
}

// allow overriding files by link() to support naive rename() implementation
#define LINK_ALLOW_OVERRIDE 1

int dummyfs_link(void *ctx, oid_t *dir, const char *name, oid_t *oid)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *d, *o, *victim_o = NULL;
	int ret;
	oid_t victim_oid;

	if (name == NULL)
		return -EINVAL;

	if (dummyfs_device(fs, dir))
		return -EINVAL;

	if ((d = object_get(fs, dir->id)) == NULL)
		return -ENOENT;

	if ((o = dummyfs_get(fs, oid)) == NULL) {
		object_put(fs, d);
		return -ENOENT;
	}

	if (!S_ISDIR(d->mode)) {
		object_put(fs, o);
		object_put(fs, d);
		return -EEXIST;
	}

	if (S_ISDIR(o->mode) && o->nlink != 0) {
		object_put(fs, o);
		object_put(fs, d);
		return -EINVAL;
	}

	o->nlink++;

	if (S_ISDIR(o->mode)) {
		object_lock(fs, o);
		dir_add(fs, o, ".", S_IFDIR | 0666, oid);
		dir_add(fs, o, "..", S_IFDIR | 0666, dir);
		o->nlink++;
		object_unlock(fs, o);
		object_lock(fs, d);
		d->nlink++;
		object_unlock(fs, d);
	}

#ifdef LINK_ALLOW_OVERRIDE
	if (dir_find(d, name, &victim_oid) > 0) {
		victim_o = object_get(fs, victim_oid.id);
		if (S_ISDIR(victim_o->mode) // explicitly disallow overwriting directories
				|| victim_oid.id == oid->id) { // linking to self
			object_put(fs, victim_o);
			victim_o = NULL;
		}
		else {
			// object_lock(victim_o); //FIXME: per-object locking
		}
	}
#endif

	object_lock(fs, d);
	if (!victim_o) {
		ret = dir_add(fs, d, name, o->mode, oid);
	}
	else {
		ret = dir_replace(d, name, oid);
		victim_o->nlink--;
		// object_unlock(fs, victim_o); //FIXME: per-object locking
	}

	if (ret != EOK) {
		object_unlock(fs, d); /* FIXME: remove after adding per-object locking */
		object_lock(fs, o);
		o->nlink--;
		if (S_ISDIR(o->mode))
			o->nlink--;
		object_unlock(fs, o);
		object_lock(fs, d); /* FIXME: remove after adding per-object locking */
	}

	d->mtime = d->atime = o->mtime = time(NULL);

	object_unlock(fs, d);
	object_put(fs, o);
	object_put(fs, d);
	object_put(fs, victim_o);

	return ret;
}


int dummyfs_unlink(void *ctx, oid_t *dir, const char *name)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	oid_t oid;
	dummyfs_object_t *o, *d;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return -EINVAL;

	if (dummyfs_device(fs, dir))
		return -EINVAL;

	d = object_get(fs, dir->id);

	if (d == NULL)
		return -EINVAL;

	object_lock(fs, d);

	if (dir_find(d, name, &oid) < 0) {
		object_unlock(fs, d);
		object_put(fs, d);
		return -ENOENT;
	}

	if (oid.id == 0) {
		object_unlock(fs, d);
		object_put(fs, d);
		return -EINVAL;
	}

	o = dummyfs_get(fs, &oid);

	if (o == NULL) {
		object_unlock(fs, d);
		object_put(fs, d);
		return -ENOENT;
	}

	if (S_ISDIR(o->mode) && dir_empty(fs, o) != EOK) {
		object_unlock(fs, d);
		object_put(fs, d);
		object_put(fs, o);
		return -ENOTEMPTY;
	}

	ret = dir_remove(fs, d, name);

	if (ret == EOK && S_ISDIR(o->mode))
		d->nlink--;

	d->mtime = d->atime = o->mtime = time(NULL);

	object_unlock(fs, d);
	object_put(fs, d);

	if (ret == EOK) {
		object_lock(fs, o);
		o->nlink--;
		if (S_ISDIR(o->mode))
			o->nlink--;
		object_unlock(fs, o);
	}
	object_put(fs, o);

	return ret;
}


int dummyfs_create(void *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
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
		o = dev_find(fs, dev, 1);
	else
		o = object_create(fs);

	if (o == NULL)
		return -ENOMEM;

	object_lock(fs, o);
	o->oid.port = fs->port;
	o->mode = mode;
	o->atime = o->mtime = o->ctime = time(NULL);

	if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))
		memcpy(oid, dev, sizeof(oid_t));
	else
		memcpy(oid, &o->oid, sizeof(oid_t));

	object_unlock(fs, o);

	if ((ret = dummyfs_link(ctx, dir, name, &o->oid)) != EOK) {
		object_put(fs, o);
		return ret;
	}

	if (S_ISLNK(mode)) {
		const char* path = name + strlen(name) + 1;
		object_lock(fs, o);
		/* TODO: remove symlink if write failed */
		dummyfs_write_internal(fs, o, 0, path, strlen(path));
		object_unlock(fs, o);
	}

	object_put(fs, o);
	return EOK;
}


int dummyfs_destroy(void *ctx, oid_t *oid)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get_unlocked(fs, oid->id);

	if (o == NULL)
		return -ENOENT;

	if ((ret = object_remove(fs, o)) == EOK) {
		if (S_ISREG(o->mode)) {
			object_lock(fs, o);
			dummyfs_truncate_internal(fs, o, 0);
			object_unlock(fs, o);
		}
		else if (S_ISDIR(o->mode))
			dir_destroy(fs, o);
		else if (S_ISCHR(o->mode) || S_ISBLK(o->mode) || S_ISFIFO(o->mode))
			dev_destroy(fs, &o->dev);

		else if (o->mode == OBJECT_MODE_MEM) {
#ifndef NOMMU
			munmap((void *)((uintptr_t)o->chunks->data & ~(_PAGE_SIZE - 1)), (o->size + (_PAGE_SIZE - 1)) & ~(_PAGE_SIZE - 1));
#endif
			free(o->chunks);
		}
		dummyfs_decsz(fs, sizeof(dummyfs_object_t));
		free(o);
	}

	return ret;
}


int dummyfs_readdir(void *ctx, oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *d;
	dummyfs_dirent_t *ei;
	offs_t diroffs = 0;
	int ret = -ENOENT;

	if (dummyfs_device(fs, dir))
		return -EINVAL;

	d = object_get(fs, dir->id);

	if (d == NULL)
		return -ENOENT;

	if (!S_ISDIR(d->mode)) {
		object_put(fs, d);
		return -EINVAL;
	}

	object_lock(fs, d);

	if ((ei = d->entries) == NULL) {
		object_unlock(fs, d);
		object_put(fs, d);
		return -EINVAL;
	}
	dent->d_reclen = 0;
	do {
		if (diroffs >= offs) {
			if ((sizeof(struct dirent) + ei->len + 1) > size) {
				object_unlock(fs, d);
				object_put(fs, d);
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

			object_unlock(fs, d);
			object_put(fs, d);
			return 	EOK;
		}
		diroffs++;
		ei = ei->next;
	} while (ei != d->entries);

	d->atime = time(NULL);

	object_unlock(fs, d);
	object_put(fs, d);

	return ret;
}


int dummyfs_open(void *ctx, oid_t *oid)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;

	if ((o = dummyfs_get(fs, oid)) == NULL)
		return -ENOENT;

	object_lock(fs, o);
	o->atime = time(NULL);

	object_unlock(fs, o);
	return EOK;
}


int dummyfs_close(void *ctx, oid_t *oid)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;

	if ((o = dummyfs_get(fs, oid)) == NULL)
		return -ENOENT;

	object_lock(fs, o);
	o->atime = time(NULL);

	object_unlock(fs, o);
	object_put(fs, o);
	object_put(fs, o);
	return EOK;
}


int dummyfs_truncate(void *ctx, oid_t *oid, size_t size)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;
	int ret;

	o = object_get(fs, oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode)) {
		object_put(fs, o);
		return -EACCES;
	}

	if (o->size == size) {
		object_put(fs, o);
		return EOK;
	}

	object_lock(fs, o);

	ret = dummyfs_truncate_internal(fs, o, size);

	object_unlock(fs, o);
	object_put(fs, o);

	return ret;
}


int dummyfs_read(void *ctx, oid_t *oid, offs_t offs, char *buff, size_t len)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	int ret = EOK;
	int readsz;
	int readoffs;
	dummyfs_chunk_t *chunk;
	dummyfs_object_t *o;

	o = object_get(fs, oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode) && !S_ISLNK(o->mode) && o->mode != OBJECT_MODE_MEM)
		ret = -EINVAL;

	if (buff == NULL)
		ret = -EINVAL;

	if (o->size <= offs) {
		object_put(fs, o);
		return 0;
	}

	if (ret != EOK) {
		object_put(fs, o);
		return ret;
	}

	if (len == 0) {
		object_put(fs, o);
		return EOK;
	}

	object_lock(fs, o);
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

		len -= readsz;
		buff += readsz;
		offs += readsz;
		ret += readsz;

		chunk = chunk->next;

	} while (len && chunk != o->chunks);

	o->atime = time(NULL);
	object_unlock(fs, o);
	object_put(fs, o);

	return ret;
}


int dummyfs_write(void *ctx, oid_t *oid, offs_t offs, const char *buff, size_t len)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;
	int ret = EOK;

	o = object_get(fs, oid->id);

	if (o == NULL)
		return -EINVAL;

	if (!S_ISREG(o->mode))
		ret = -EINVAL;

	if (buff == NULL)
		ret = -EINVAL;

	if (ret != EOK) {
		object_put(fs, o);
		return ret;
	}

	object_lock(fs, o);

	ret = dummyfs_write_internal(fs, o, offs, buff, len);

	object_unlock(fs, o);
	object_put(fs, o);

	return ret;
}


int dummyfs_createMapped(void *ctx, oid_t *dir, const char *name, void *addr, size_t size, oid_t *oid)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o;
	dummyfs_chunk_t *chunk;

	if (dummyfs_create(ctx, dir, name, oid, 0755, otFile, NULL) != 0)
		return -ENOMEM;

	if ((o = object_get(fs, oid->id)) == NULL) {
		dummyfs_destroy(ctx, oid);
		return -EINVAL;
	}

	if ((chunk = malloc(sizeof(dummyfs_chunk_t))) == NULL) {
		object_put(fs, o);
		dummyfs_destroy(ctx, oid);
		return -ENOMEM;
	}

#ifndef NOMMU
	addr = mmap(NULL, (size + (_PAGE_SIZE - 1)) & ~(_PAGE_SIZE - 1), 0x1 | 0x2, 0, OID_PHYSMEM, (addr_t)addr);
	if (addr == MAP_FAILED) {
		free(chunk);
		object_put(fs, o);
		dummyfs_destroy(ctx, oid);
		return -ENOMEM;
	}
#endif

	chunk->offs = 0;
	chunk->size = size;
	chunk->used = size;
	chunk->data = (void *)((uintptr_t)addr & ~(_PAGE_SIZE - 1)) + ((uintptr_t)addr & (_PAGE_SIZE - 1));
	chunk->next = chunk;
	chunk->prev = chunk;

	object_lock(fs, o);
	o->size = size;
	o->mode = OBJECT_MODE_MEM;
	o->chunks = chunk;
	object_unlock(fs, o);
	object_put(fs, o);

	return EOK;
}


int dummyfs_statfs(void *ctx, void *buf, size_t len)
{
	dummyfs_t *fs = ctx;
	struct statvfs *st = buf;

	if ((st == NULL) || (len != sizeof(*st))) {
		return -EINVAL;
	}

	/* TODO: fs->size access should be protected with a lock */
	st->f_bsize = st->f_frsize = 1;
	st->f_blocks = DUMMYFS_SIZE_MAX;
	st->f_bavail = st->f_bfree = st->f_blocks - fs->size;
	st->f_files = 0;
	st->f_ffree = 0;
	st->f_favail = 0;
	st->f_fsid = (unsigned long)fs; /* TODO: filesystem ID should be generated at mount time */
	st->f_flag = 0;                 /* TODO: mount options should be saved at mount time */
	st->f_namemax = 255;            /* TODO: define max filename limit, use 255 limit for now */

	return EOK;
}


int dummyfs_mount(void **ctx, const char *data, unsigned long mode, oid_t *root)
{
	dummyfs_t *fs;
	dummyfs_object_t *o;
	oid_t rootdir;

	if ((fs = calloc(1, sizeof(dummyfs_t))) == NULL)
		return -ENOMEM;

	fs->port = root->port;

	if (mutexCreate(&fs->mutex) != EOK) {
		free(fs);
		return -ENOMEM;
	}

	if (object_init(fs) != EOK) {
		resourceDestroy(fs->mutex);
		free(fs);
		return -ENOMEM;
	}

	if (dev_init(fs) != EOK) {
		object_cleanup(fs);
		resourceDestroy(fs->mutex);
		free(fs);
		return -ENOMEM;
	}

	/* Create root directory */
	if ((o = object_create(fs)) == NULL) {
		dev_cleanup(fs);
		object_cleanup(fs);
		resourceDestroy(fs->mutex);
		free(fs);
		return -ENOMEM;
	}

	o->oid.port = fs->port;
	o->mode = S_IFDIR | 0666;

	memcpy(&rootdir, &o->oid, sizeof(oid_t));
	if (dir_add(fs, o, ".", S_IFDIR | 0666, &rootdir) != EOK) {
		dev_cleanup(fs);
		object_cleanup(fs);
		resourceDestroy(fs->mutex);
		free(fs);
		return -ENOMEM;
	}

	if (dir_add(fs, o, "..", S_IFDIR | 0666, &rootdir) != EOK) {
		dev_cleanup(fs);
		object_cleanup(fs);
		resourceDestroy(fs->mutex);
		free(fs);
		return -ENOMEM;
	}

	*ctx = fs;

	return EOK;
}


int dummyfs_unmount(void *ctx)
{
	dummyfs_t *fs = (dummyfs_t *)ctx;

	/* Assume, no other thread uses this filesystem */
	dev_cleanup(fs);
	object_cleanup(fs);

	resourceDestroy(fs->mutex);
	free(fs);

	return EOK;
}
