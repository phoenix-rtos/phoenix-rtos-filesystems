/*
 * Phoenix-RTOS
 *
 * LittleFS library
 *
 * Copyright 2019, 2020, 2024 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski, Jacek Maksymowicz
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

#include <liblfs.h>

#include "ph_lfs_api.h"

#define TRACE_CALLS(x)

static int liblfs_create(void *info, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	lfs_t *fs = (lfs_t *)info;
	int ret;
	TRACE_CALLS(puts(__FUNCTION__));
	mutexLock(fs->mutex);
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
			if (!LFS_ISDEV(mode)) {
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

	ret = ph_lfs_create(fs, dir->id, name, mode, dev, &oid->id);

	if ((ret >= 0) && (type == otSymlink)) {
		const char *target = name + strlen(name) + 1;
		size_t targetlen = strlen(target);
		int err = ph_lfs_open(fs, oid->id);
		if (err >= 0) {
			err = ph_lfs_write(fs, oid->id, 0, target, targetlen);
		}

		if (err >= 0) {
			err = ph_lfs_close(fs, oid->id);
		}

		if (err < 0) {
			ret = err;
			ph_lfs_destroy(fs, oid->id);
			oid->id = LFS_INVALID_PHID;
		}
	}

	mutexUnlock(fs->mutex);
	return ret;
}


static int liblfs_open(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_open(lfs, oid->id);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_close(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_close(lfs, oid->id);
	mutexUnlock(lfs->mutex);
	return ret;
}


static ssize_t liblfs_read(void *info, oid_t *oid, off_t offs, void *data, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_read(lfs, oid->id, offs, data, len);
	mutexUnlock(lfs->mutex);
	return ret;
}


static ssize_t liblfs_write(void *info, oid_t *oid, off_t offs, const void *data, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_write(lfs, oid->id, offs, data, len);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_setattr(void *info, oid_t *oid, int type, long long attr, void *data, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_setattr(lfs, oid->id, type, attr, data, len);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_getattr(void *info, oid_t *oid, int type, long long *attr)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_getattr(lfs, oid->id, type, attr);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_truncate(void *info, oid_t *oid, size_t size)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_truncate(lfs, oid->id, size);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_destroy(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_destroy(lfs, oid->id);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_lookup(void *info, oid_t *oid, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	res->port = lfs->port;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_lookup(lfs, oid->id, name, &res->id, dev);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_link(void *info, oid_t *dir, const char *name, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_link(lfs, dir->id, name, oid->id);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_unlink(void *info, oid_t *oid, const char *name)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_unlink(lfs, oid->id, name);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_readdir(void *info, oid_t *oid, off_t offs, struct dirent *dent, size_t size)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_readdir(lfs, oid->id, offs, dent, size);
	mutexUnlock(lfs->mutex);
	return ret;
}


static int liblfs_statfs(void *info, void *buf, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	struct statvfs *st = buf;
	if ((st == NULL) || (len != sizeof(*st))) {
		return -EINVAL;
	}

	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_statfs(lfs, st);
	mutexUnlock(lfs->mutex);
	return ret;
}

static int liblfs_sync(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->mutex);
	int ret = ph_lfs_sync(lfs, oid->id);
	mutexUnlock(lfs->mutex);
	return ret;
}


static void liblfs_devctl(void *info, oid_t *oid, const void *i, void *o)
{
	(void)oid;
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	const liblfs_devctl_in_t *in = (const liblfs_devctl_in_t *)i;
	liblfs_devctl_out_t *out = (liblfs_devctl_out_t *)o;

	mutexLock(lfs->mutex);
	bool ro = lfs->cfg->ph.readOnly != 0;
	switch (in->command) {
		case LIBLFS_DEVCTL_FS_GROW:
			out->err = ro ? -EROFS : lfs_fs_grow(lfs, in->fsGrow.targetSize);
			break;

		case LIBLFS_DEVCTL_FS_GC:
			out->err = lfs_fs_gc(lfs);
			break;

		default:
			out->err = -EINVAL;
			break;
	};

	mutexUnlock(lfs->mutex);
}


int liblfs_handler(void *fdata, msg_t *msg)
{
	bool ro = ((lfs_t *)fdata)->cfg->ph.readOnly != 0;
	switch (msg->type) {
		case mtCreate:
			msg->o.create.err = ro ? -EROFS : liblfs_create(fdata, &msg->i.create.dir, msg->i.data, &msg->o.create.oid, msg->i.create.mode, msg->i.create.type, &msg->i.create.dev);
			break;

		case mtDestroy:
			msg->o.io.err = ro ? -EROFS : liblfs_destroy(fdata, &msg->i.destroy.oid);
			break;

		case mtLookup:
			msg->o.lookup.err = liblfs_lookup(fdata, &msg->i.lookup.dir, msg->i.data, &msg->o.lookup.fil, &msg->o.lookup.dev, msg->o.data, msg->o.size);
			break;

		case mtOpen:
			msg->o.io.err = liblfs_open(fdata, &msg->i.openclose.oid);
			break;

		case mtClose:
			msg->o.io.err = liblfs_close(fdata, &msg->i.openclose.oid);
			break;

		case mtRead:
			msg->o.io.err = liblfs_read(fdata, &msg->i.io.oid, msg->i.io.offs, msg->o.data, msg->o.size);
			break;

		case mtReaddir:
			msg->o.io.err = liblfs_readdir(fdata, &msg->i.readdir.dir, msg->i.readdir.offs, msg->o.data, msg->o.size);
			break;

		case mtWrite:
			msg->o.io.err = ro ? -EROFS : liblfs_write(fdata, &msg->i.io.oid, msg->i.io.offs, msg->i.data, msg->i.size);
			break;

		case mtTruncate:
			msg->o.io.err = ro ? -EROFS : liblfs_truncate(fdata, &msg->i.io.oid, msg->i.io.len);
			break;

		case mtDevCtl:
			liblfs_devctl(fdata, &msg->i.io.oid, msg->i.raw, msg->o.raw);
			msg->o.io.err = EOK;
			break;

		case mtGetAttr:
			msg->o.attr.err = liblfs_getattr(fdata, &msg->i.attr.oid, msg->i.attr.type, &msg->o.attr.val);
			break;

		case mtSetAttr:
			msg->o.attr.err = liblfs_setattr(fdata, &msg->i.attr.oid, msg->i.attr.type, msg->i.attr.val, msg->i.data, msg->i.size);
			break;

		case mtLink:
			msg->o.io.err = ro ? -EROFS : liblfs_link(fdata, &msg->i.ln.dir, msg->i.data, &msg->i.ln.oid);
			break;

		case mtUnlink:
			msg->o.io.err = ro ? -EROFS : liblfs_unlink(fdata, &msg->i.ln.dir, msg->i.data);
			break;

		case mtStat:
			msg->o.io.err = liblfs_statfs(fdata, msg->o.data, msg->o.size);
			break;

		case mtSync:
			msg->o.io.err = liblfs_sync(fdata, &msg->i.io.oid);
			break;

		default:
			break;
	}

	return EOK;
}


typedef struct {
	id_t id;
	ssize_t (*read)(id_t, off_t, char *, size_t);
	ssize_t (*write)(id_t, off_t, const char *, size_t);
} liblfs_diskCtx;


int liblfs_unmount(void *fdata)
{
	lfs_t *lfs = (lfs_t *)fdata;
	ph_lfs_unmount(lfs);
	resourceDestroy(lfs->mutex);
	free(lfs->cfg->context);
	free((void *)lfs->cfg);
	free(lfs);

	return EOK;
}


static int liblfs_setConfig(struct lfs_config *cfg, size_t storageSize, unsigned long mode)
{
	int blockSizeShift = mode & LIBLFS_BLOCK_SIZE_LOG_MASK;
	if (blockSizeShift == 0) {
		return -EINVAL;
	}

	/* Block device configuration*/
	cfg->read_size = 256;
	cfg->prog_size = 32;
	cfg->block_size = 1 << blockSizeShift;
	cfg->block_count = storageSize / cfg->block_size;

	/* Runtime configuration */
	/* NOTE: cache size also determines max size of file that can be inlined in directory structure */
	/* Any file not inlined WILL take at least 1 full block of storage. */
	cfg->cache_size = 256;
	cfg->lookahead_size = 16;
	cfg->block_cycles = 500;
	cfg->ph.maxCachedObjects = 10;
	cfg->ph.useCTime = ((mode & LIBLFS_USE_CTIME_FLAG) != 0) ? 1 : 0;
	cfg->ph.useMTime = ((mode & LIBLFS_USE_MTIME_FLAG) != 0) ? 1 : 0;
	cfg->ph.useATime = ((mode & LIBLFS_USE_ATIME_FLAG) != 0) ? 1 : 0;
	cfg->ph.readOnly = ((mode & LIBLFS_READ_ONLY_FLAG) != 0) ? 1 : 0;
	return 0;
}


static int liblfs_mountCommon(lfs_t **infoOut, const struct lfs_config *cfg, unsigned int port)
{
	lfs_t *info = (lfs_t *)malloc(sizeof(lfs_t));
	if (info == NULL) {
		return -ENOMEM;
	}

	int err = ph_lfs_mount(info, cfg, port);
	if (err < 0) {
		free(info);
		return err;
	}

	err = mutexCreate(&info->mutex);
	if (err < 0) {
		free(info);
		return err;
	}

	*infoOut = info;
	return 0;
}


static int liblfs_blkDevRead(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	liblfs_diskCtx *ctx = (liblfs_diskCtx *)c->context;
	int ret = ctx->read(ctx->id, block * c->block_size + off, buffer, size);
	if (ret < 0) {
		return ret;
	}

	return (ret == size) ? 0 : -1;
}


static int liblfs_blkDevProg(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	if (c->ph.readOnly != 0) {
		printf("Trying to erase on ROFS\n");
		return -EROFS;
	}

	liblfs_diskCtx *ctx = (liblfs_diskCtx *)c->context;
	int ret = ctx->write(ctx->id, block * c->block_size + off, buffer, size);
	if (ret < 0) {
		return ret;
	}

	return (ret == size) ? 0 : -1;
}


static int liblfs_blkDevErase(const struct lfs_config *c, lfs_block_t block)
{
	if (c->ph.readOnly != 0) {
		printf("Trying to erase on ROFS\n");
		return -EROFS;
	}

	return 0;
}


static int liblfs_blkDevSync(const struct lfs_config *c)
{
	return 0;
}


extern int liblfs_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, off_t, char *, size_t),
	ssize_t (*write)(id_t, off_t, const char *, size_t), void **fdata)
{
	/* Must be initialized to 0 to load defaults */
	struct lfs_config *cfg = (struct lfs_config *)calloc(1, sizeof(struct lfs_config));
	liblfs_diskCtx *diskCtx = malloc(sizeof(liblfs_diskCtx));
	/* Block device driver functions */
	diskCtx->id = dev->id;
	diskCtx->read = read;
	diskCtx->write = write;
	cfg->context = diskCtx;
	cfg->read = liblfs_blkDevRead;
	cfg->prog = liblfs_blkDevProg;
	cfg->erase = liblfs_blkDevErase;
	cfg->sync = liblfs_blkDevSync;

	/* This is hard-coded because pc-ata mount interface is too limited
	 * to pass any useful arguments here */
	unsigned long mode = (12 & LIBLFS_BLOCK_SIZE_LOG_MASK) | LIBLFS_USE_CTIME_FLAG | LIBLFS_USE_MTIME_FLAG;
	int err = liblfs_setConfig(cfg, 64 * 1024 * 1024, mode);
	if (err < 0) {
		free(cfg);
		return err;
	}

	err = liblfs_mountCommon((lfs_t **)fdata, cfg, dev->port);
	if (err < 0) {
		free(cfg);
		return err;
	}

	return LFS_ROOT_PHID;
}


const static storage_fsops_t fsOps = {
	.open = liblfs_open,
	.close = liblfs_close,
	.read = liblfs_read,
	.write = liblfs_write,
	.setattr = liblfs_setattr,
	.getattr = liblfs_getattr,
	.truncate = liblfs_truncate,
	.devctl = liblfs_devctl,
	.create = liblfs_create,
	.destroy = liblfs_destroy,
	.lookup = liblfs_lookup,
	.link = liblfs_link,
	.unlink = liblfs_unlink,
	.readdir = liblfs_readdir,
	.statfs = liblfs_statfs,
	.sync = liblfs_sync,
};


const static storage_fsops_t fsOpsReadOnly = {
	.open = liblfs_open,
	.close = liblfs_close,
	.read = liblfs_read,
	.write = NULL,
	.setattr = liblfs_setattr,
	.getattr = liblfs_getattr,
	.truncate = NULL,
	.devctl = liblfs_devctl,
	.create = NULL,
	.destroy = NULL,
	.lookup = liblfs_lookup,
	.link = NULL,
	.unlink = NULL,
	.readdir = liblfs_readdir,
	.statfs = liblfs_statfs,
	.sync = liblfs_sync,
};


int liblfs_storage_umount(storage_fs_t *strg_fs)
{
	lfs_t *lfs = (lfs_t *)strg_fs->info;
	ph_lfs_unmount(lfs);
	free((void *)lfs->cfg);
	free(lfs);

	return EOK;
}


static int liblfs_mtdDevRead(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	storage_t *strg = (storage_t *)c->context;
	size_t retlen;
	int ret = strg->dev->mtd->ops->read(strg, block * c->block_size + off, buffer, size, &retlen);
	if (ret < 0) {
		return ret;
	}

	return (retlen == size) ? 0 : -1;
}


static int liblfs_mtdDevProg(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	storage_t *strg = (storage_t *)c->context;
	size_t retlen;
	int ret = strg->dev->mtd->ops->write(strg, block * c->block_size + off, buffer, size, &retlen);
	if (ret < 0) {
		return ret;
	}

	return (retlen == size) ? 0 : -1;
}


static int liblfs_mtdDevErase(const struct lfs_config *c, lfs_block_t block)
{
	storage_t *strg = (storage_t *)c->context;
	return strg->dev->mtd->ops->erase(strg, block * c->block_size, 1);
}


static int liblfs_mtdDevSync(const struct lfs_config *c)
{
	/* Not necessary for MTD */
	return 0;
}


int liblfs_storage_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root)
{
	if (strg == NULL ||
		strg->dev == NULL ||
		strg->dev->mtd == NULL ||
		strg->dev->mtd->ops == NULL ||
		strg->dev->mtd->ops->read == NULL ||
		strg->dev->mtd->ops->write == NULL ||
		strg->dev->mtd->ops->erase == NULL) {
		return -ENOSYS;
	}

	/* Must be initialized to 0 to load defaults */
	struct lfs_config *cfg = (struct lfs_config *)calloc(1, sizeof(struct lfs_config));
	/* Block device driver functions */
	cfg->context = strg;
	cfg->read = liblfs_mtdDevRead;
	cfg->prog = liblfs_mtdDevProg;
	cfg->erase = liblfs_mtdDevErase;
	cfg->sync = liblfs_mtdDevSync;

	int err = liblfs_setConfig(cfg, strg->size, mode);
	if (err < 0) {
		free(cfg);
		return err;
	}

	err = liblfs_mountCommon((lfs_t **)&fs->info, cfg, root->port);
	if (err < 0) {
		free(cfg);
		return err;
	}

	root->id = LFS_ROOT_PHID;
	fs->ops = (cfg->ph.readOnly != 0) ? &fsOpsReadOnly : &fsOps;
	return EOK;
}
