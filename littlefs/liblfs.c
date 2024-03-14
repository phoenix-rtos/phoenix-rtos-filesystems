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
#include <sys/minmax.h>
#include <sys/stat.h>
#include <sys/threads.h>

#include <liblfs.h>

#include "ph_lfs_api.h"
#include "ph_lfs_types.h"

#define TRACE_CALLS(x)

static int liblfs_create(void *info, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	lfs_t *lfs = (lfs_t *)info;
	if (lfs->cfg->ph.readOnly != 0) {
		return -EROFS;
	}

	TRACE_CALLS(puts(__FUNCTION__));
	mutexLock(lfs->ph.mutex);
	oid->port = lfs->ph.port;

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

	int ret = ph_lfs_create(lfs, dir->id, name, mode, dev, &oid->id);

	if ((ret >= 0) && (type == otSymlink)) {
		const char *target = name + strlen(name) + 1;
		size_t targetlen = strlen(target);
		int err = ph_lfs_open(lfs, oid->id);
		if (err >= 0) {
			err = ph_lfs_write(lfs, oid->id, 0, target, targetlen);
		}

		if (err >= 0) {
			err = ph_lfs_close(lfs, oid->id);
		}

		if (err < 0) {
			ret = err;
			ph_lfs_destroy(lfs, oid->id);
			oid->id = LFS_INVALID_PHID;
		}
	}

	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_open(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_open(lfs, oid->id);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_close(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_close(lfs, oid->id);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static ssize_t liblfs_read(void *info, oid_t *oid, off_t offs, void *data, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_read(lfs, oid->id, offs, data, len);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static ssize_t liblfs_write(void *info, oid_t *oid, off_t offs, const void *data, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	if (lfs->cfg->ph.readOnly != 0) {
		return -EROFS;
	}

	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_write(lfs, oid->id, offs, data, len);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_setattr(void *info, oid_t *oid, int type, long long attr, const void *data, size_t len)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_setattr(lfs, oid->id, type, attr, data, len);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_getattr(void *info, oid_t *oid, int type, long long *attr)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_getattr(lfs, oid->id, type, attr);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_getattrAll(void *info, oid_t *oid, struct _attrAll *attrs)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_getattrAll(lfs, oid->id, attrs);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_truncate(void *info, oid_t *oid, size_t size)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	if (lfs->cfg->ph.readOnly != 0) {
		return -EROFS;
	}

	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_truncate(lfs, oid->id, size);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_destroy(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	if (lfs->cfg->ph.readOnly != 0) {
		return -EROFS;
	}

	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_destroy(lfs, oid->id);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_lookup(void *info, oid_t *oid, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	res->port = lfs->ph.port;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_lookup(lfs, oid->id, name, &res->id, dev);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_link(void *info, oid_t *dir, const char *name, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	if (lfs->cfg->ph.readOnly != 0) {
		return -EROFS;
	}

	int ret = -ENOSYS;
	if (LIBLFS_LINK_IS_RENAME != 0) {
		mutexLock(lfs->ph.mutex);
		ret = ph_lfs_rename(lfs, dir->id, oid->id, name);
		mutexUnlock(lfs->ph.mutex);
	}

	return ret;
}


static int liblfs_unlink(void *info, oid_t *oid, const char *name)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	if (lfs->cfg->ph.readOnly != 0) {
		return -EROFS;
	}

	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_unlink(lfs, oid->id, name);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_readdir(void *info, oid_t *oid, off_t offs, struct dirent *dent, size_t size)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_readdir(lfs, oid->id, offs, dent, size);
	mutexUnlock(lfs->ph.mutex);
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
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_statfs(lfs, st);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}

static int liblfs_sync(void *info, oid_t *oid)
{
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	mutexLock(lfs->ph.mutex);
	int ret = ph_lfs_sync(lfs, oid->id);
	mutexUnlock(lfs->ph.mutex);
	return ret;
}


static int liblfs_devctl(void *info, oid_t *oid, const void *i, void *o)
{
	int ret;
	(void)oid;
	TRACE_CALLS(puts(__FUNCTION__));
	lfs_t *lfs = (lfs_t *)info;
	const liblfs_devctl_in_t *in = (const liblfs_devctl_in_t *)i;

	mutexLock(lfs->ph.mutex);
	bool ro = lfs->cfg->ph.readOnly != 0;
	switch (in->command) {
		case LIBLFS_DEVCTL_FS_GROW:
			if (ro) {
				ret = -EROFS;
			}
			else if (in->fsGrow.targetSize == 0) {
				ret = lfs_fs_grow(lfs, lfs->cfg->block_count);
			}
			else {
				ret = lfs_fs_grow(lfs, in->fsGrow.targetSize);
			}
			break;

		case LIBLFS_DEVCTL_FS_GC:
			ret = lfs_fs_gc(lfs);
			break;

		default:
			ret = -EINVAL;
			break;
	};

	mutexUnlock(lfs->ph.mutex);
	return ret;
}


int liblfs_handler(void *fdata, msg_t *msg)
{
	switch (msg->type) {
		case mtCreate:
			msg->o.err = liblfs_create(fdata, &msg->oid, msg->i.data, &msg->o.create.oid, msg->i.create.mode, msg->i.create.type, &msg->i.create.dev);
			break;

		case mtDestroy:
			msg->o.err = liblfs_destroy(fdata, &msg->oid);
			break;

		case mtLookup:
			msg->o.err = liblfs_lookup(fdata, &msg->oid, msg->i.data, &msg->o.lookup.fil, &msg->o.lookup.dev, msg->o.data, msg->o.size);
			break;

		case mtOpen:
			msg->o.err = liblfs_open(fdata, &msg->oid);
			break;

		case mtClose:
			msg->o.err = liblfs_close(fdata, &msg->oid);
			break;

		case mtRead:
			msg->o.err = liblfs_read(fdata, &msg->oid, msg->i.io.offs, msg->o.data, msg->o.size);
			break;

		case mtReaddir:
			msg->o.err = liblfs_readdir(fdata, &msg->oid, msg->i.readdir.offs, msg->o.data, msg->o.size);
			break;

		case mtWrite:
			msg->o.err = liblfs_write(fdata, &msg->oid, msg->i.io.offs, msg->i.data, msg->i.size);
			break;

		case mtTruncate:
			msg->o.err = liblfs_truncate(fdata, &msg->oid, msg->i.io.len);
			break;

		case mtDevCtl:
			msg->o.err = liblfs_devctl(fdata, &msg->oid, msg->i.raw, msg->o.raw);
			break;

		case mtGetAttr:
			msg->o.err = liblfs_getattr(fdata, &msg->oid, msg->i.attr.type, &msg->o.attr.val);
			break;

		case mtGetAttrAll: {
			struct _attrAll *attrs = msg->o.data;
			if ((attrs == NULL) || (msg->o.size < sizeof(struct _attrAll))) {
				msg->o.err = -EINVAL;
			}
			else {
				msg->o.err = liblfs_getattrAll(fdata, &msg->oid, attrs);
			}
			break;
		}

		case mtSetAttr:
			msg->o.err = liblfs_setattr(fdata, &msg->oid, msg->i.attr.type, msg->i.attr.val, msg->i.data, msg->i.size);
			break;

		case mtLink:
			msg->o.err = liblfs_link(fdata, &msg->oid, msg->i.data, &msg->i.ln.oid);
			break;

		case mtUnlink:
			msg->o.err = liblfs_unlink(fdata, &msg->oid, msg->i.data);
			break;

		case mtStat:
			msg->o.err = liblfs_statfs(fdata, msg->o.data, msg->o.size);
			break;

		case mtSync:
			msg->o.err = liblfs_sync(fdata, &msg->oid);
			break;

		default:
			break;
	}

	return 0;
}


typedef struct {
	id_t id;
	ssize_t (*read)(id_t, off_t, char *, size_t);
	ssize_t (*write)(id_t, off_t, const char *, size_t);
} liblfs_diskCtx;


int liblfs_ata_unmount(void *fdata)
{
	lfs_t *lfs = (lfs_t *)fdata;
	ph_lfs_unmount(lfs);
	resourceDestroy(lfs->ph.mutex);
	free(lfs->cfg->context);
	free((void *)lfs->cfg);
	free(lfs);

	return 0;
}


/* Parses configuration given through the arguments and verifies it.
 * Note: `storageSize` == 0 is acceptable - in that case the size of storage is read from LFS superblock.
 * However, it will prevent `lfs_format` from working. */
static int liblfs_setConfig(struct lfs_config *cfg, const char *args, size_t storageSize, size_t eraseSize,
		size_t writeSize, unsigned long mode)
{
	/* TODO: additional data can be passed as string - currently unused, but could be used
	 * for overrides (block size, cache size, etc.) */
	(void)args;
	cfg->block_size = eraseSize;
	cfg->prog_size = writeSize;
	/* prog_size must be at least 4 - otherwise the driver breaks */
	cfg->prog_size = max(cfg->prog_size, 4);
	/* Assume there are alignment requirements for reading are the same as for writing */
	cfg->read_size = cfg->prog_size;

	/* Block device configuration*/
	cfg->block_count = storageSize / cfg->block_size;

	/* Runtime configuration */
	cfg->cache_size = LIBLFS_CACHESIZE_DEFAULT;
	cfg->lookahead_size = LIBLFS_LOOKAHEAD_SIZE;
	cfg->block_cycles = LIBLFS_CYCLES_THRESHOLD;
	cfg->ph.objectEvictThreshold = LIBLFS_N_CACHED_OBJECTS;
	cfg->ph.useCTime = ((mode & LIBLFS_USE_CTIME_FLAG) != 0) ? 1 : 0;
	cfg->ph.useMTime = ((mode & LIBLFS_USE_MTIME_FLAG) != 0) ? 1 : 0;
	cfg->ph.useATime = ((mode & LIBLFS_USE_ATIME_FLAG) != 0) ? 1 : 0;
	cfg->ph.readOnly = ((mode & LIBLFS_READ_ONLY_FLAG) != 0) ? 1 : 0;
	if ((cfg->block_size == 0) ||
			(cfg->prog_size == 0) ||
			(cfg->cache_size == 0) ||
			(cfg->block_size % cfg->cache_size != 0) ||
			(cfg->cache_size % cfg->prog_size != 0) ||
			(cfg->cache_size % cfg->read_size != 0)) {
		return -EINVAL;
	}

	return 0;
}


static int liblfs_mountCommon(lfs_t **infoOut, const struct lfs_config *cfg, unsigned int port, bool doFormat)
{
	lfs_t *info = (lfs_t *)malloc(sizeof(lfs_t));
	if (info == NULL) {
		return -ENOMEM;
	}

	int err;
#if LIBLFS_MOUNT_FORMAT_OPTION
	if (doFormat) {
		err = lfs_format(info, cfg);
		if (err < 0) {
			return err;
		}
	}
#else
	if (doFormat) {
		err = -EINVAL;
	}
#endif

	err = ph_lfs_mount(info, cfg, port);
#if LIBLFS_FORMAT_ON_MOUNT_FAILURE
	if (err == -EILSEQ) {
		err = lfs_format(info, cfg);
		if (err >= 0) {
			err = ph_lfs_mount(info, cfg, port);
		}
	}
#endif

	if (err < 0) {
		free(info);
		return err;
	}

	err = mutexCreate(&info->ph.mutex);
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
		return -EROFS;
	}

	(void)block;
	/* LFS supports no-op erase */
	return 0;
}


static int liblfs_noOpSync(const struct lfs_config *c)
{
	(void)c;
	return 0;
}


int liblfs_ata_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, off_t, char *, size_t),
		ssize_t (*write)(id_t, off_t, const char *, size_t), void **fdata)
{
	/* Must be initialized to 0 to load defaults */
	struct lfs_config *cfg = (struct lfs_config *)calloc(1, sizeof(struct lfs_config));
	liblfs_diskCtx *diskCtx = malloc(sizeof(liblfs_diskCtx));
	if ((cfg == NULL) || (diskCtx == NULL)) {
		free(cfg);
		free(diskCtx);
		return -ENOMEM;
	}

	/* Block device driver functions */
	diskCtx->id = dev->id;
	diskCtx->read = read;
	diskCtx->write = write;
	cfg->context = diskCtx;
	cfg->read = liblfs_blkDevRead;
	cfg->prog = liblfs_blkDevProg;
	cfg->erase = liblfs_blkDevErase;
	cfg->sync = liblfs_noOpSync;

	/* This is hard-coded because pc-ata mount interface is too limited to pass any useful arguments here */
	unsigned long mode = LIBLFS_USE_CTIME_FLAG | LIBLFS_USE_MTIME_FLAG;
	int err = liblfs_setConfig(cfg, NULL, 0, 4096, 16, mode);
	if (err < 0) {
		free(cfg);
		free(diskCtx);
		return err;
	}

	err = liblfs_mountCommon((lfs_t **)fdata, cfg, dev->port, false);
	if (err < 0) {
		free(cfg);
		free(diskCtx);
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
	.getattrall = liblfs_getattrAll,
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


int liblfs_storage_umount(storage_fs_t *strg_fs)
{
	lfs_t *lfs = (lfs_t *)strg_fs->info;
	ph_lfs_unmount(lfs);
	resourceDestroy(lfs->ph.mutex);
	free((void *)lfs->cfg);
	free(lfs);

	return 0;
}


static int liblfs_mtdDevRead(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	storage_t *strg = (storage_t *)c->context;
	size_t retlen;
	int ret = strg->dev->mtd->ops->read(strg, strg->start + block * c->block_size + off, buffer, size, &retlen);
	if (ret < 0) {
		return ret;
	}

	return (retlen == size) ? 0 : -1;
}


static int liblfs_mtdDevProg(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	storage_t *strg = (storage_t *)c->context;
	size_t retlen;
	int ret = strg->dev->mtd->ops->write(strg, strg->start + block * c->block_size + off, buffer, size, &retlen);
	if (ret < 0) {
		return ret;
	}

	return (retlen == size) ? 0 : -1;
}


static int liblfs_mtdDevErase(const struct lfs_config *c, lfs_block_t block)
{
	storage_t *strg = (storage_t *)c->context;
	int ret = strg->dev->mtd->ops->erase(strg, strg->start + (block * c->block_size), c->block_size);
	return (ret < 0) ? ret : 0;
}


static int liblfs_mtdDevSync(const struct lfs_config *c)
{
	storage_t *strg = (storage_t *)c->context;
	strg->dev->mtd->ops->sync(strg);
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
		return -EINVAL;
	}

	/* Must be initialized to 0 to load defaults */
	struct lfs_config *cfg = (struct lfs_config *)calloc(1, sizeof(struct lfs_config));
	if (cfg == NULL) {
		return -ENOMEM;
	}

	/* Block device driver functions */
	cfg->context = strg;
	cfg->read = liblfs_mtdDevRead;
	cfg->prog = liblfs_mtdDevProg;
	cfg->erase = liblfs_mtdDevErase;
	if (strg->dev->mtd->ops->sync != NULL) {
		cfg->sync = liblfs_mtdDevSync;
	}
	else {
		/* If sync function is NULL, assume it's not necessary */
		cfg->sync = liblfs_noOpSync;
	}

	int err = liblfs_setConfig(
			cfg,
			data,
			strg->size,
			strg->dev->mtd->erasesz,
			strg->dev->mtd->writesz,
			mode);
	if (err < 0) {
		free(cfg);
		return err;
	}

	bool doFormat = (data != NULL) && (strcmp("format", data) == 0);
	err = liblfs_mountCommon((lfs_t **)&fs->info, cfg, root->port, doFormat);
	if (err < 0) {
		free(cfg);
		return err;
	}

	root->id = LFS_ROOT_PHID;
	fs->ops = &fsOps;
	return 0;
}


int liblfs_rawcfg_mount(void **fs_handle, oid_t *root, const struct lfs_config *cfg, int doFormat)
{
	if (cfg->prog_size < 4) {
		return -EINVAL;
	}

	int err = liblfs_mountCommon((lfs_t **)fs_handle, cfg, root->port, doFormat != 0);
	if (err < 0) {
		return err;
	}

	root->id = LFS_ROOT_PHID;
	return 0;
}


int liblfs_rawcfg_unmount(void *fs_handle, int freeCfg)
{
	lfs_t *lfs = (lfs_t *)fs_handle;
	ph_lfs_unmount(lfs);
	if (freeCfg != 0) {
		free((void *)lfs->cfg);
	}

	resourceDestroy(lfs->ph.mutex);
	free(lfs);

	return 0;
}
