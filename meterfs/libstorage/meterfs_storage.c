/*
 * Phoenix-RTOS
 *
 * Meterfs MTD device adapter
 *
 * Copyright 2023 Phoenix Systems
 * Author: Hubert Badocha
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <meterfs.h>
#include <sys/threads.h>

#include "meterfs_storage.h"

/* clang-format off */
#define LOG_INFO(str, ...) do { if(1) {(void)printf(str "\n", ##__VA_ARGS__);} } while(0)
/* clang-format on */

struct _meterfs_devCtx_t {
	storage_t *storage;
};


typedef struct {
	handle_t lock;
	meterfs_ctx_t meterfsCtx;
	struct _meterfs_devCtx_t devCtx;
} meterfs_partition_t;


/* Temporary adapters to implement storage fsops interfaces */


static int fsOpAdapter_open(void *info, oid_t *oid)
{
	int err;
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;

	(void)mutexLock(ctx->lock);

	err = meterfs_open(oid->id, &ctx->meterfsCtx);

	(void)mutexUnlock(ctx->lock);
	return err;
}


static int fsOpAdapter_close(void *info, oid_t *oid)
{
	int err;
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;

	(void)mutexLock(ctx->lock);

	err = meterfs_close(oid->id, &ctx->meterfsCtx);

	(void)mutexUnlock(ctx->lock);
	return err;
}


static ssize_t fsOpAdapter_read(void *info, oid_t *oid, offs_t offs, void *data, size_t len)
{
	int err;
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;

	(void)mutexLock(ctx->lock);

	err = meterfs_readFile(oid->id, offs, data, len, &ctx->meterfsCtx);

	(void)mutexUnlock(ctx->lock);
	return err;
}


static ssize_t fsOpAdapter_write(void *info, oid_t *oid, offs_t offs, const void *data, size_t len)
{
	int err;
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;

	(void)offs;

	(void)mutexLock(ctx->lock);

	err = meterfs_writeFile(oid->id, data, len, &ctx->meterfsCtx);

	(void)mutexUnlock(ctx->lock);
	return err;
}


static void fsOpAdapter_devctl(void *info, oid_t *oid, const void *i, void *o)
{
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;
	const meterfs_i_devctl_t *in = (const meterfs_i_devctl_t *)i;
	meterfs_o_devctl_t *out = (meterfs_o_devctl_t *)o;

	(void)oid;

	(void)mutexLock(ctx->lock);

	out->err = meterfs_devctl(in, out, &ctx->meterfsCtx);

	(void)mutexUnlock(ctx->lock);
}


static int fsOpAdapter_lookup(void *info, oid_t *dir, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	int err;
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;

	(void)lnk;
	(void)lnksz;

	(void)mutexLock(ctx->lock);

	res->port = dir->port;
	err = meterfs_lookup(name, &res->id, &ctx->meterfsCtx);
	*dev = *res;

	(void)mutexUnlock(ctx->lock);
	return err;
}


static ssize_t meterfsAdapter_read(struct _meterfs_devCtx_t *devCtx, off_t offs, void *buff, size_t bufflen)
{
	size_t retlen;
	int err;

	err = devCtx->storage->dev->mtd->ops->read(devCtx->storage, offs, buff, bufflen, &retlen);
	if (err < 0) {
		return err;
	}
	return retlen;
}


static ssize_t meterfsAdapter_write(struct _meterfs_devCtx_t *devCtx, off_t offs, const void *buff, size_t bufflen)
{
	size_t retlen;
	int err;

	err = devCtx->storage->dev->mtd->ops->write(devCtx->storage, offs, buff, bufflen, &retlen);
	if (err < 0) {
		return err;
	}
	return retlen;
}


static int meterfsAdapter_eraseSector(struct _meterfs_devCtx_t *devCtx, off_t offs)
{
	return devCtx->storage->dev->mtd->ops->erase(devCtx->storage, offs, devCtx->storage->dev->mtd->erasesz);
}


static void meterfsAdapter_powerCtrl(struct _meterfs_devCtx_t *devCtx, int state)
{
	int err;

	switch (state) {
		case 0:
			if (devCtx->storage->dev->mtd->ops->suspend != NULL) {
				err = devCtx->storage->dev->mtd->ops->suspend(devCtx->storage);
				if (err < 0) {
					LOG_INFO("meterfs_mtd: Error suspending device, code: %d.", err);
				}
			}
			break;
		case 1:
			if (devCtx->storage->dev->mtd->ops->resume != NULL) {
				devCtx->storage->dev->mtd->ops->resume(devCtx->storage);
			}
			break;
		default:
			LOG_INFO("meterfs_mtd: powerCtrl adapter encountered unexpected state: %d.", state);
			break;
	}
}


int meterfs_mount(storage_t *storage, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root)
{
	meterfs_partition_t *ctx;
	int err;
	/* TODO implement missing functions. */
	static const storage_fsops_t fsOps = {
		.open = fsOpAdapter_open,
		.close = fsOpAdapter_close,
		.read = fsOpAdapter_read,
		.write = fsOpAdapter_write,
		.setattr = NULL,
		.getattr = NULL,
		.truncate = NULL,
		.devctl = fsOpAdapter_devctl,
		.create = NULL,
		.destroy = NULL,
		.lookup = fsOpAdapter_lookup,
		.link = NULL,
		.unlink = NULL,
		.readdir = NULL,
		.statfs = NULL,
		.sync = NULL
	};

	(void)mode;
	(void)data;

	/* clang-format off */
	if ((storage == NULL) || (fs == NULL) ||
			(storage->dev == NULL) || (storage->dev->mtd == NULL) || (storage->dev->mtd->ops == NULL) ||
			(storage->dev->mtd->ops->read == NULL) || (storage->dev->mtd->ops->write == NULL) || (storage->dev->mtd->ops->erase == NULL) ||
			(storage->dev->mtd->writesz != 1) || (storage->dev->mtd->type != mtd_norFlash)) {
		return -EINVAL;
	}
	/* clang-format on */

	ctx = calloc(1, sizeof(meterfs_partition_t));
	if (ctx == NULL) {
		return -ENOMEM;
	}
	err = mutexCreate(&ctx->lock);
	if (err < 0) {
		free(ctx);
		return -ENOMEM;
	}

	ctx->devCtx.storage = storage;
	ctx->meterfsCtx.sz = storage->size;
	ctx->meterfsCtx.offset = storage->start;
	ctx->meterfsCtx.sectorsz = storage->dev->mtd->erasesz;
	ctx->meterfsCtx.read = meterfsAdapter_read;
	ctx->meterfsCtx.write = meterfsAdapter_write;
	ctx->meterfsCtx.eraseSector = meterfsAdapter_eraseSector;
	ctx->meterfsCtx.powerCtrl = meterfsAdapter_powerCtrl;
	ctx->meterfsCtx.devCtx = &ctx->devCtx;

	err = meterfs_init(&ctx->meterfsCtx);
	if (err < 0) {
		resourceDestroy(ctx->lock);
		free(ctx);
		return -EINVAL;
	}

	root->id = 0;
	fs->info = ctx;
	fs->ops = &fsOps;

	return EOK;
}


int meterfs_umount(storage_fs_t *fs)
{
	meterfs_partition_t *ctx = (meterfs_partition_t *)fs->info;
	int err;

	err = resourceDestroy(ctx->lock);
	free(ctx);

	return err;
}
