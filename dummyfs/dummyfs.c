/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018, 2021, 2023 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk,
 * Kamil Amanowicz, Maciej Purski, Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <poll.h>
#include <assert.h>
#include <sys/minmax.h>
#include <sys/stat.h> /* to set mode for / */
#include <sys/statvfs.h>
#include <sys/list.h>
#include <sys/mount.h>
#include <sys/threads.h>
#include <sys/mman.h>

#include "dummyfs.h"
#include "dir.h"
#include "object.h"
#include "memory.h"

/* higher two bytes used as magic number */
#define OBJECT_MODE_MEM (0xabad0000 | S_IFREG | S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define DUMMYFS_ROOTID  0


static inline int dummyfs_isDevice(dummyfs_t *ctx, oid_t *oid)
{
	return (oid->port != ctx->port) ? 1 : 0;
}


int dummyfs_lookup(void *ctx, oid_t *dir, const char *name, oid_t *res, oid_t *dev)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	dummyfs_object_t *o, *d;
	int len = 0;
	int err = -ENOENT;

	mutexLock(fs->mutex);
	if (dir == NULL) {
		oid_t root = { .port = fs->port, .id = DUMMYFS_ROOTID };
		d = dummyfs_object_get(fs, &root);
		assert(d != NULL);
	}
	else if (dummyfs_isDevice(fs, dir) != 0) {
		mutexUnlock(fs->mutex);
		return -EINVAL;
	}
	else {
		d = dummyfs_object_get(fs, dir);
		if (d == NULL) {
			mutexUnlock(fs->mutex);
			return -ENOENT;
		}
	}

	while (name[len] != '\0') {
		while (name[len] == '/') {
			len++;
		}

		/* check again for path ending */
		if (name[len] == '\0') {
			break;
		}

#if 1 /* FIXME: this is a hack to handle an unproperly mounted fs by 'bind' */
		char *end = strchrnul(name + len, '/');
		const size_t size = end - name + len;
		if ((strncmp(name + len, "..", size) == 0) && (d->oid.id == DUMMYFS_ROOTID)) {
			*res = fs->parent;
			*dev = fs->parent;
			dummyfs_object_put(fs, d);
			mutexUnlock(fs->mutex);
			return len - 1;
		}
#endif

		if (!S_ISDIR(d->mode)) {
			/* We're not finished, but not a directory, fail */
			err = -ENOTDIR;
			break;
		}

		/* Check if mountpoint */
		if (d->oid.port != d->dev.port) {
			*res = d->oid;
			len--;
			err = 0;
			break;
		}

		err = dummyfs_dir_find(d, name + len, res);
		if (err <= 0) {
			break;
		}

		len += err;

		if (dummyfs_isDevice(fs, res) != 0) {
			break;
		}

		dummyfs_object_put(fs, d);
		d = dummyfs_object_get(fs, res);
	}

	dummyfs_object_put(fs, d);

	if (err < 0) {
		mutexUnlock(fs->mutex);
		return err;
	}

	o = dummyfs_object_get(fs, res);
	*dev = o->dev;
	dummyfs_object_put(fs, o);
	mutexUnlock(fs->mutex);

	return len;
}


static int _dummyfs_truncateObject(dummyfs_t *fs, dummyfs_object_t *o, size_t size)
{
	TRACE();
	/* Lazy: start allocating memory only after first write */
	if ((size == o->size) || (o->data == NULL) || (o->chunks == NULL)) {
		o->size = size;
		return 0;
	}

	if (o->mode == OBJECT_MODE_MEM) {
		if (size != 0) {
			return -EINVAL;
		}
		else {
			munmap(o->data, (o->size + (_PAGE_SIZE - 1)) & ~(_PAGE_SIZE - 1));
			o->data = 0;
			return 0;
		}
	}

	if (o->size < DUMMYFS_CHUNKSZ) {
		/* Small file */
		if (size >= DUMMYFS_CHUNKSZ) {
			/* Make a big file */
			void *page = dummyfs_mmap(fs);
			if (page == NULL) {
				return -ENOMEM;
			}

			void **chunks = dummyfs_calloc(fs, sizeof(void *) * DUMMYFS_CHUNKCNT(size));
			if (chunks == NULL) {
				dummyfs_munmap(fs, page);
				return -ENOMEM;
			}

			memcpy(page, o->data, o->size);

			dummyfs_free(fs, o->data, o->size);

			chunks[0] = page;
			o->chunks = chunks;
		}
		else {
			void *tptr = dummyfs_realloc(fs, o->data, o->size, size);
			if ((size != 0) && (tptr == NULL)) {
				return -ENOMEM;
			}

			o->data = tptr;

			if (size > o->size) {
				memset((char *)o->data + o->size, 0, size - o->size);
			}
		}
	}
	else {
		/* Big files */
		if (size < DUMMYFS_CHUNKSZ) {
			/* Make small file */
			void *tptr = NULL;

			if (size != 0) {
				tptr = dummyfs_malloc(fs, size);
				if (tptr == NULL) {
					return -ENOMEM;
				}

				memcpy(tptr, o->chunks[0], size);
			}

			size_t limit = DUMMYFS_CHUNKCNT(o->size);
			for (size_t i = 0; i < limit; ++i) {
				if (o->chunks[i] != NULL) {
					dummyfs_munmap(fs, o->chunks[i]);
				}
			}

			dummyfs_free(fs, o->chunks, sizeof(void *) * DUMMYFS_CHUNKCNT(o->size));

			o->data = tptr;
		}
		else {
			if (DUMMYFS_CHUNKCNT(size) != DUMMYFS_CHUNKCNT(o->size)) {
				/* Will loop only in case of size < o->size */
				size_t limit = DUMMYFS_CHUNKCNT(o->size);
				for (size_t i = DUMMYFS_CHUNKCNT(size); i < limit; ++i) {
					/* realloc to a smaller size is guaranteed to succeed,
					 * so it's safe to munmap before realloc */
					if (o->chunks[i] != NULL) {
						dummyfs_munmap(fs, o->chunks[i]);
					}
				}
				void **tchunk = dummyfs_realloc(fs, o->chunks,
					sizeof(void *) * DUMMYFS_CHUNKCNT(o->size),
					sizeof(void *) * DUMMYFS_CHUNKCNT(size));
				if (tchunk == NULL) {
					return -ENOMEM;
				}

				limit = DUMMYFS_CHUNKIDX(size - 1);
				for (size_t i = DUMMYFS_CHUNKIDX(o->size - 1) + 1; i <= limit; ++i) {
					tchunk[i] = NULL;
				}

				o->chunks = tchunk;
			}
		}
	}

	o->size = size;

	return 0;
}


static int _dummyfs_truncate(dummyfs_t *fs, oid_t *oid, size_t size)
{
	TRACE();
	dummyfs_object_t *o;
	int ret = 0;

	o = dummyfs_object_get(fs, oid);
	if (o == NULL) {
		return -EINVAL;
	}

	if (o->mode == OBJECT_MODE_MEM) {
		return -EPERM;
	}

	if (!S_ISREG(o->mode)) {
		if (S_ISDIR(o->mode)) {
			ret = -EISDIR;
		}
		else {
			ret = -EACCES;
		}
		dummyfs_object_put(fs, o);
		return ret;
	}

	o->atime = time(NULL);

	ret = _dummyfs_truncateObject(fs, o, size);
	if (ret == 0) {
		o->mtime = o->atime;
	}

	dummyfs_object_put(fs, o);

	return ret;
}


int dummyfs_truncate(void *ctx, oid_t *oid, size_t size)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	mutexLock(fs->mutex);
	int ret = _dummyfs_truncate(fs, oid, size);
	mutexUnlock(fs->mutex);
	return (ret == -ENOMEM) ? -ENOSPC : ret;
}


int dummyfs_setattr(void *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	int ret = 0;

	mutexLock(fs->mutex);
	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o != NULL) {
		switch (type) {
			case atUid:
				o->uid = attr;
				break;

			case atGid:
				o->gid = attr;
				break;

			case atMode:
				o->mode = (o->mode & ~ALLPERMS) | (attr & ALLPERMS);
				break;

			case atSize:
				ret = _dummyfs_truncate(ctx, oid, attr);
				break;

			case atDev:
				if ((data != NULL) && (size == sizeof(oid_t)) && S_ISDIR(o->mode) && (dummyfs_isDevice(fs, &o->dev) == 0)) {
					memcpy(&o->dev, data, sizeof(oid_t));
				}
				else {
					ret = -EINVAL;
				}
				break;

			case atMTime:
				o->mtime = attr;
				break;

			case atATime:
				o->atime = attr;
				break;

			default:
				ret = -EINVAL;
				break;
		}

		/* FIXME: mtime and atime are always set together */
		if ((ret == 0) && (type != atMTime) && (type != atATime)) {
			o->mtime = time(NULL);
		}
		dummyfs_object_put(fs, o);
	}
	else {
		ret = -ENOENT;
	}
	mutexUnlock(fs->mutex);

	return ret;
}


int dummyfs_getattr(void *ctx, oid_t *oid, int type, long long *attr)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	int ret = 0;

	mutexLock(fs->mutex);
	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o != NULL) {
		switch (type) {
			case atUid:
				*attr = o->uid;
				break;

			case atGid:
				*attr = o->gid;
				break;

			case atMode:
				*attr = o->mode;
				break;

			case atSize:
				*attr = o->size;
				break;

			case atBlocks:
				*attr = (o->size + S_BLKSIZE - 1) / S_BLKSIZE;
				break;

			case atIOBlock:
				*attr = DUMMYFS_CHUNKSZ;
				break;

			case atType:
				/* TODO - remove redundant custom enums */
				if (S_ISDIR(o->mode)) {
					*attr = otDir;
				}
				else if (S_ISREG(o->mode)) {
					*attr = otFile;
				}
				else if (S_ISCHR(o->mode) || S_ISBLK(o->mode) || S_ISFIFO(o->mode)) {
					*attr = otDev;
				}
				else if (S_ISLNK(o->mode)) {
					*attr = otSymlink;
				}
				else {
					*attr = otUnknown;
				}
				break;

			case atCTime:
				*attr = o->ctime;
				break;

			case atMTime:
				*attr = o->mtime;
				break;

			case atATime:
				*attr = o->atime;
				break;

			case atLinks:
				*attr = o->nlink;
				break;

			case atPollStatus:
				/* Always ready */
				*attr = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
				break;

			default:
				ret = -EINVAL;
				break;
		}

		dummyfs_object_put(fs, o);
	}
	else {
		ret = -ENOENT;
	}
	mutexUnlock(fs->mutex);

	return ret;
}

/* allow overriding files by link() to support naive rename() implementation */
#define LINK_ALLOW_OVERRIDE 1

static int _dummyfs_link(dummyfs_t *fs, oid_t *dir, const char *name, oid_t *oid)
{
	TRACE();
	int ret;

	if ((name == NULL) || (dummyfs_isDevice(fs, dir) != 0)) {
		return -EINVAL;
	}

	dummyfs_object_t *d = dummyfs_object_get(fs, dir);
	if (d == NULL) {
		return -ENOENT;
	}

	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o == NULL) {
		dummyfs_object_put(fs, d);
		return -ENOENT;
	}

	if (!S_ISDIR(d->mode)) {
		dummyfs_object_put(fs, o);
		dummyfs_object_put(fs, d);
		return -EEXIST;
	}

	if (S_ISDIR(o->mode) && (o->nlink != 0)) {
		dummyfs_object_put(fs, o);
		dummyfs_object_put(fs, d);
		return -EINVAL;
	}

	o->nlink++;

	if (S_ISDIR(o->mode)) {
		ret = dummyfs_dir_init(fs, o);
		if (ret < 0) {
			dummyfs_object_put(fs, o);
			dummyfs_object_put(fs, d);
			return ret;
		}
		ret = dummyfs_dir_add(fs, o, ".", S_IFDIR | DEFFILEMODE, oid);
		if (ret < 0) {
			dummyfs_object_put(fs, o);
			dummyfs_object_put(fs, d);
			return ret;
		}
		ret = dummyfs_dir_add(fs, o, "..", S_IFDIR | DEFFILEMODE, dir);
		if (ret < 0) {
			dummyfs_object_put(fs, o);
			dummyfs_object_put(fs, d);
			return ret;
		}
		o->nlink++;
		d->nlink++;
	}

	int replaced = 0;
#ifdef LINK_ALLOW_OVERRIDE
	oid_t victim_oid;
	if (dummyfs_dir_find(d, name, &victim_oid) > 0) {
		dummyfs_object_t *victim_o = dummyfs_object_get(fs, &victim_oid);
		assert(victim_o != NULL);
		/* Explicitly disallow overwriting directories and linking to self */
		if (!S_ISDIR(victim_o->mode) && (victim_oid.id != oid->id)) {
			ret = dummyfs_dir_replace(d, name, oid);
			victim_o->nlink--;
			replaced = 1;
		}
		dummyfs_object_put(fs, victim_o);
	}
#endif

	if (replaced == 0) {
		ret = dummyfs_dir_add(fs, d, name, o->mode, oid);
	}

	if (ret != 0) {
		o->nlink--;
		if (S_ISDIR(o->mode)) {
			o->nlink--;
		}
	}

	d->mtime = time(NULL);
	d->atime = d->mtime;
	o->mtime = d->mtime;

	dummyfs_object_put(fs, o);
	dummyfs_object_put(fs, d);

	return ret;
}


int dummyfs_link(void *ctx, oid_t *dir, const char *name, oid_t *oid)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	mutexLock(fs->mutex);
	int ret = _dummyfs_link(fs, dir, name, oid);
	mutexUnlock(fs->mutex);
	return (ret == -ENOMEM) ? -ENOSPC : ret;
}


static int _dummyfs_unlink(dummyfs_t *fs, oid_t *dir, const char *name)
{
	TRACE();
	oid_t oid;

	/* clang-format off */
	if ((name == NULL) ||
			(strcmp(name, ".") == 0) || (strcmp(name, "..") == 0) ||
			(dummyfs_isDevice(fs, dir) != 0)) {
		return -EINVAL;
	}
	/* clang-format on */

	dummyfs_object_t *d = dummyfs_object_get(fs, dir);
	if (d == NULL) {
		return -EINVAL;
	}

	if (dummyfs_dir_find(d, name, &oid) < 0) {
		dummyfs_object_put(fs, d);
		return -ENOENT;
	}

	if (oid.id == DUMMYFS_ROOTID) {
		dummyfs_object_put(fs, d);
		return -EINVAL;
	}

	dummyfs_object_t *o = dummyfs_object_get(fs, &oid);
	assert(o != NULL);

	if (S_ISDIR(o->mode) && (dummyfs_dir_empty(fs, o) != 0)) {
		dummyfs_object_put(fs, d);
		dummyfs_object_put(fs, o);
		return -ENOTEMPTY;
	}

	int ret = dummyfs_dir_remove(fs, d, name);
	if (ret == 0) {
		o->nlink--;
		if (S_ISDIR(o->mode)) {
			o->nlink--;
			d->nlink--;
		}
	}

	d->mtime = time(NULL);
	d->atime = d->mtime;
	o->mtime = d->mtime;

	dummyfs_object_put(fs, d);
	dummyfs_object_put(fs, o);

	return ret;
}


int dummyfs_unlink(void *ctx, oid_t *dir, const char *name)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	mutexLock(fs->mutex);
	int ret = _dummyfs_unlink(fs, dir, name);
	mutexUnlock(fs->mutex);
	return ret;
}


static int _dummyfs_writeObject(dummyfs_t *fs, dummyfs_object_t *o, offs_t offs, const char *buff, size_t len);


static int _dummyfs_create(dummyfs_t *fs, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	TRACE();
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

		default:
			break;
	}

	/* Check if file exist */
	dummyfs_object_t *d = dummyfs_object_get(fs, dir);
	if (d == NULL) {
		return -ENOENT;
	}
	oid_t existing;
	int ret = dummyfs_dir_find(d, name, &existing);
	dummyfs_object_put(fs, d);
	if (ret > 0) {
		/* We shouldn't receive mtCreate on existing file, always error */
		return -EEXIST;
	}

	dummyfs_object_t *o = dummyfs_object_create(fs);
	if (o == NULL) {
		return -ENOMEM;
	}

	o->oid.port = fs->port;
	o->mode = mode;
	o->atime = time(NULL);
	o->mtime = o->atime;
	o->ctime = o->atime;
	o->dev = (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode)) ? *dev : o->oid;

	*oid = o->oid;

	ret = _dummyfs_link(fs, dir, name, &o->oid);
	if (ret != 0) {
		dummyfs_object_put(fs, o);
		return ret;
	}

	if (S_ISLNK(mode)) {
		/* Two strings are provided, seconds one is the target, see symlink() */
		const char *path = name + strlen(name) + 1;
		if (_dummyfs_writeObject(fs, o, 0, path, strlen(path)) < 0) {
			(void)_dummyfs_unlink(fs, dir, name);
			ret = -EIO;
		}
	}

	dummyfs_object_put(fs, o);

	return ret;
}


int dummyfs_create(void *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	mutexLock(fs->mutex);
	int ret = _dummyfs_create(fs, dir, name, oid, mode, type, dev);
	mutexUnlock(fs->mutex);
	return (ret == -ENOMEM) ? -ENOSPC : ret;
}


int _dummyfs_destroy(dummyfs_t *fs, oid_t *oid)
{
	TRACE();
	dummyfs_object_t *o = dummyfs_object_find(fs, oid);
	if (o == NULL) {
		return -ENOENT;
	}

	int ret = dummyfs_object_remove(fs, o);
	if (ret == 0) {
		if (S_ISREG(o->mode)) {
			(void)_dummyfs_truncateObject(fs, o, 0);
		}
		else if (S_ISDIR(o->mode)) {
			dummyfs_dir_destroy(fs, o);
		}
		else if (o->mode == OBJECT_MODE_MEM) {
			munmap(o->data, (o->size + (_PAGE_SIZE - 1)) & ~(_PAGE_SIZE - 1));
		}
		dummyfs_free(fs, o, sizeof(dummyfs_object_t));
	}

	return ret;
}


int dummyfs_destroy(void *ctx, oid_t *oid)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	mutexLock(fs->mutex);
	int ret = _dummyfs_destroy(fs, oid);
	mutexUnlock(fs->mutex);
	return ret;
}


int dummyfs_readdir(void *ctx, oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;

	if (dummyfs_isDevice(fs, dir) != 0) {
		return -EINVAL;
	}

	mutexLock(fs->mutex);
	dummyfs_object_t *d = dummyfs_object_get(fs, dir);
	if (d == NULL) {
		mutexUnlock(fs->mutex);
		return -ENOENT;
	}

	if (!S_ISDIR(d->mode) || (offs < 0)) {
		dummyfs_object_put(fs, d);
		mutexUnlock(fs->mutex);
		return -EINVAL;
	}

	dummyfs_dirent_t *ei;
	dummyfs_dirent_t *etree;
	off_t diroffs = 0;
	if ((d->dir.hint.entry != NULL) && (offs >= d->dir.hint.offs)) {
		diroffs = d->dir.hint.offs;
		ei = d->dir.hint.entry;
		etree = ei;
		while (etree->prev != NULL) {
			etree = etree->prev;
		}
	}
	else {
		ei = lib_treeof(dummyfs_dirent_t, linkage, lib_rbMinimum(d->dir.tree.root));
		etree = ei;
	}

	while ((diroffs < offs) && (ei != NULL)) {
		diroffs += ei->len;

		if (ei->next != NULL) {
			ei = ei->next;
		}
		else {
			ei = lib_treeof(dummyfs_dirent_t, linkage, lib_rbNext(&etree->linkage));
			etree = ei;
		}
	}

	if (ei == NULL) {
		dummyfs_object_put(fs, d);
		mutexUnlock(fs->mutex);
		return -ENOENT;
	}

	if ((sizeof(struct dirent) + ei->len + 1) > size) {
		dummyfs_object_put(fs, d);
		mutexUnlock(fs->mutex);
		return -EINVAL;
	}

	d->dir.hint.offs = diroffs;
	d->dir.hint.entry = ei;

	dent->d_ino = ei->oid.id;
	dent->d_reclen = ei->len;
	dent->d_namlen = ei->len;
	dent->d_type = ei->type;
	strcpy(dent->d_name, ei->name);

	dummyfs_object_put(fs, d);
	mutexUnlock(fs->mutex);

	return 0;
}


int dummyfs_open(void *ctx, oid_t *oid)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;

	mutexLock(fs->mutex);
	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o == NULL) {
		mutexUnlock(fs->mutex);
		return -ENOENT;
	}

	o->atime = time(NULL);

	mutexUnlock(fs->mutex);
	return 0;
}


int dummyfs_close(void *ctx, oid_t *oid)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;

	mutexLock(fs->mutex);
	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o == NULL) {
		mutexUnlock(fs->mutex);
		return -ENOENT;
	}

	o->atime = time(NULL);

	dummyfs_object_put(fs, o);
	dummyfs_object_put(fs, o);
	mutexUnlock(fs->mutex);
	return EOK;
}


int dummyfs_read(void *ctx, oid_t *oid, offs_t offs, char *buff, size_t len)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;
	size_t cnt = 0;

	mutexLock(fs->mutex);
	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o == NULL) {
		mutexUnlock(fs->mutex);
		return -EINVAL;
	}

	o->atime = time(NULL);

	if (S_ISDIR(o->mode)) {
		dummyfs_object_put(fs, o);
		mutexUnlock(fs->mutex);
		return -EISDIR;
	}

	if ((offs < 0) || (!S_ISREG(o->mode) && !S_ISLNK(o->mode) && (o->mode != OBJECT_MODE_MEM))) {
		dummyfs_object_put(fs, o);
		mutexUnlock(fs->mutex);
		return -EINVAL;
	}

	if (((offs_t)o->size <= offs) || (len == 0)) {
		dummyfs_object_put(fs, o);
		mutexUnlock(fs->mutex);
		return 0;
	}

	size_t left = min(len, (o->size - offs));
	if ((o->data == NULL) || (o->chunks == NULL)) {
		memset(buff, 0, left);
		cnt = left;
	}
	else if ((o->size < DUMMYFS_CHUNKSZ) || (o->mode == OBJECT_MODE_MEM)) {
		/* Small file */
		memcpy(buff, o->data + offs, left);
		cnt = left;
	}
	else {
		/* Big file */
		size_t foffs = (size_t)offs;
		size_t boffs = 0;
		while (left > 0) {
			void *chunk = o->chunks[DUMMYFS_CHUNKIDX(foffs)];
			size_t chunkoffs = foffs % DUMMYFS_CHUNKSZ;
			size_t cpylen = min((DUMMYFS_CHUNKSZ - chunkoffs), left);
			if (chunk == NULL) {
				memset((char *)buff + boffs, 0, cpylen);
			}
			else {
				memcpy((char *)buff + boffs, (char *)chunk + chunkoffs, cpylen);
			}

			left -= cpylen;
			boffs += cpylen;
			foffs += cpylen;
			cnt += cpylen;
		}
	}

	dummyfs_object_put(fs, o);
	mutexUnlock(fs->mutex);

	return (int)cnt; /* FIXME: Should be ssize_t */
}


static int _dummyfs_writeObject(dummyfs_t *fs, dummyfs_object_t *o, offs_t offs, const char *buff, size_t len)
{
	TRACE();
	size_t cnt = 0;

	if (o->mode == OBJECT_MODE_MEM) {
		return -EPERM;
	}

	if (len == 0) {
		return 0;
	}

	size_t oldsz = o->size;

	/* clang-format off */
	if ((offs + len) > o->size) {
		int ret = _dummyfs_truncateObject(fs, o, offs + len);
		if (ret < 0) {
			return ret;
		}
	}
	/* clang-format on */

	if ((o->size < DUMMYFS_CHUNKSZ) || (o->mode == OBJECT_MODE_MEM)) {
		/* Small file */
		if (o->data == NULL) {
			o->data = dummyfs_calloc(fs, o->size);
			if (o->data == NULL) {
				(void)_dummyfs_truncateObject(fs, o, oldsz);
				return -ENOMEM;
			}
		}
		memcpy((char *)o->data + offs, buff, len);
		cnt = len;
	}
	else {
		/* Big file */
		if (o->chunks == NULL) {
			size_t tabsz = sizeof(void *) * DUMMYFS_CHUNKCNT(o->size);
			o->chunks = dummyfs_calloc(fs, tabsz);
			if (o->chunks == NULL) {
				(void)_dummyfs_truncateObject(fs, o, oldsz);
				return -ENOMEM;
			}
		}

		/* Preallocate chunks to avoid ENOMEM on partial write */
		size_t limit = DUMMYFS_CHUNKIDX(offs + len - 1);
		for (size_t i = DUMMYFS_CHUNKIDX(offs); i <= limit; ++i) {
			if (o->chunks[i] == NULL) {
				o->chunks[i] = dummyfs_mmap(fs);
				if (o->chunks[i] == NULL) {
					(void)_dummyfs_truncateObject(fs, o, oldsz);
					return -ENOMEM;
				}
			}
		}

		size_t left = len;
		size_t foffs = (size_t)offs;
		size_t boffs = 0;
		while (left > 0) {
			void *chunk = o->chunks[DUMMYFS_CHUNKIDX(foffs)];
			size_t chunkoffs = foffs % DUMMYFS_CHUNKSZ;
			size_t cpylen = min((DUMMYFS_CHUNKSZ - chunkoffs), left);

			memcpy((char *)chunk + chunkoffs, (const char *)buff + boffs, cpylen);

			left -= cpylen;
			boffs += cpylen;
			foffs += cpylen;
			cnt += cpylen;
		}
	}

	return (int)cnt; /* FIXME: Should be ssize_t */
}


int dummyfs_write(void *ctx, oid_t *oid, offs_t offs, const char *buff, size_t len)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;

	mutexLock(fs->mutex);
	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	if (o == NULL) {
		mutexUnlock(fs->mutex);
		return -EINVAL;
	}
	int ret = _dummyfs_writeObject(fs, o, offs, buff, len);
	dummyfs_object_put(fs, o);
	mutexUnlock(fs->mutex);

	return (ret == -ENOMEM) ? -ENOSPC : ret;
}


int dummyfs_createMapped(void *ctx, oid_t *dir, const char *name, void *addr, size_t size, oid_t *oid)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;

	mutexLock(fs->mutex);
	int ret = _dummyfs_create(fs, dir, name, oid, 0755, otFile, NULL);
	if (ret < 0) {
		mutexUnlock(fs->mutex);
		return ret;
	}

	dummyfs_object_t *o = dummyfs_object_get(fs, oid);
	assert(o != NULL);

	o->data = mmap(NULL, (size + (_PAGE_SIZE - 1)) & ~(_PAGE_SIZE - 1), PROT_READ | PROT_WRITE, MAP_PHYSMEM | MAP_ANONYMOUS, -1, (addr_t)addr);
	o->size = size;
	o->mode = OBJECT_MODE_MEM;

	dummyfs_object_put(fs, o);
	mutexUnlock(fs->mutex);

	return 0;
}


int dummyfs_statfs(void *ctx, void *buf, size_t len)
{
	TRACE();
	dummyfs_t *fs = ctx;
	struct statvfs *st = buf;

	if ((st == NULL) || (len != sizeof(*st))) {
		return -EINVAL;
	}

	mutexLock(fs->mutex);
	st->f_bsize = st->f_frsize = 1;
	st->f_blocks = DUMMYFS_SIZE_MAX;
	st->f_bavail = st->f_bfree = st->f_blocks - fs->size;
	st->f_files = 0;
	st->f_ffree = 0;
	st->f_favail = 0;
	st->f_fsid = (unsigned long)fs; /* TODO: filesystem ID should be generated at mount time */
	st->f_flag = fs->mode;
	st->f_namemax = 255; /* TODO: define max filename limit, use 255 limit for now */
	mutexUnlock(fs->mutex);

	return 0;
}


static int dummyfs_alloc(dummyfs_t *fs, const char *data, unsigned long mode)
{
	TRACE();
	if (mutexCreate(&fs->mutex) != 0) {
		fs->mutex = 0;
		return -ENOMEM;
	}

	if (dummyfs_object_init(fs) != 0) {
		return -ENOMEM;
	}

	/* Create root directory */
	dummyfs_object_t *o = dummyfs_object_create(fs);
	if (o == NULL) {
		return -ENOMEM;
	}

	o->oid.port = fs->port;
	o->oid.id = DUMMYFS_ROOTID;
	o->mode = S_IFDIR | DEFFILEMODE;
	int ret = dummyfs_dir_init(fs, o);
	if (ret < 0) {
		return ret;
	}

	oid_t rootdir = o->oid;
	fs->parent = rootdir;
	fs->mode = mode;

	if (data != NULL) {
		fs->mountpt = resolve_path(data, NULL, 1, 0);
		if (fs->mountpt == NULL) {
			return -ENOMEM;
		}

		size_t mntlen = strlen(fs->mountpt);
		char *parent = malloc(mntlen + 3 + 1);
		if (parent == NULL) {
			return -ENOMEM;
		}
		memcpy(parent, fs->mountpt, mntlen + 1);
		strcpy(parent + mntlen, "/..");
		ret = lookup(parent, &fs->parent, NULL);
		free(parent);
		if (ret < 0) {
			return -ENOENT;
		}

		void *mountPoint = realloc(fs->mountpt, mntlen + 1);
		if (mountPoint == NULL) {
			return -ENOMEM;
		}
		fs->mountpt = mountPoint;
	}
	o->dev = o->oid;

	ret = dummyfs_dir_add(fs, o, ".", S_IFDIR | DEFFILEMODE, &rootdir);
	if (ret < 0) {
		return ret;
	}

	ret = dummyfs_dir_add(fs, o, "..", S_IFDIR | DEFFILEMODE, &fs->parent);
	if (ret < 0) {
		return ret;
	}

	return 0;
}


static void dummyfs_cleanup(dummyfs_t *fs)
{
	TRACE();
	if (fs->mountpt != NULL) {
		free(fs->mountpt);
	}

	if (fs->dummytree.root != NULL) {
		dummyfs_object_cleanup(fs);
	}

	if (fs->mutex != 0) {
		resourceDestroy(fs->mutex);
	}

	free(fs);
}


int dummyfs_mount(void **ctx, const char *data, unsigned long mode, oid_t *root)
{
	TRACE();

	dummyfs_t *fs = calloc(1, sizeof(dummyfs_t));
	if (fs == NULL) {
		return -ENOMEM;
	}

	fs->mountpt = NULL;
	fs->port = root->port;

	int ret = dummyfs_alloc(fs, data, mode);
	if (ret < 0) {
		dummyfs_cleanup(fs);
		return ret;
	}
	*ctx = fs;

	return ret;
}


int dummyfs_unmount(void *ctx)
{
	TRACE();
	dummyfs_t *fs = (dummyfs_t *)ctx;

	/* Assume, no other thread uses this filesystem */
	dummyfs_cleanup(fs);

	return 0;
}
