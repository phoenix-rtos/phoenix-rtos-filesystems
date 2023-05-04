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
#include <inttypes.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <meterfs.h>
#include <sys/threads.h>
#include <mtd/mtd.h>

#include "meterfs_storage.h"

/* clang-format off */
#define LOG_INFO(str, ...) do { if(1) {(void)printf(str "\n", ##__VA_ARGS__);} } while(0)
/* clang-format on */

struct _meterfs_devCtx_t {
	struct mtd_info mtd;
	struct {
		struct erase_info instr;
		handle_t lock;
		handle_t cond;
		volatile int res;
	} erase;
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


static int fsOpAdapter_devctl(void *info, oid_t *oid, const void *i, void *o)
{
	meterfs_partition_t *ctx = (meterfs_partition_t *)info;
	const meterfs_i_devctl_t *in = (const meterfs_i_devctl_t *)i;
	meterfs_o_devctl_t *out = (meterfs_o_devctl_t *)o;

	(void)oid;

	(void)mutexLock(ctx->lock);

	out->err = meterfs_devctl(in, out, &ctx->meterfsCtx);

	(void)mutexUnlock(ctx->lock);
	return out->err;
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

	err = mtd_read(&devCtx->mtd, offs, bufflen, &retlen, (unsigned char *)buff);
	if (err < 0) {
		return err;
	}
	return retlen;
}


static ssize_t meterfsAdapter_write(struct _meterfs_devCtx_t *devCtx, off_t offs, const void *buff, size_t bufflen)
{
	size_t retlen;
	int err;

	err = mtd_write(&devCtx->mtd, offs, bufflen, &retlen, (unsigned char *)buff);
	if (err < 0) {
		return err;
	}
	return retlen;
}


static void meterfsAdapter_eraseSectorCallback(struct erase_info *instr)
{
	struct _meterfs_devCtx_t *devCtx = (struct _meterfs_devCtx_t *)instr->priv;

	if (instr->state != MTD_ERASE_DONE) {
		LOG_INFO("meterfs_mtd: Erase at %#10" PRIx64 " finished, but state != MTD_ERASE_DONE. State is 0x%x instead.\n",
			instr->addr, instr->state);
		devCtx->erase.res = -EIO;
	}
	else {
		devCtx->erase.res = 0;
	}
	(void)condSignal(devCtx->erase.cond);
}


static int meterfsAdapter_eraseSector(struct _meterfs_devCtx_t *devCtx, off_t offs)
{
	int err;

	(void)memset(&devCtx->erase.instr, 0, sizeof(devCtx->erase.instr));

	devCtx->erase.instr.mtd = &devCtx->mtd;
	devCtx->erase.instr.addr = offs;
	devCtx->erase.instr.len = devCtx->mtd.erasesize;
	devCtx->erase.instr.callback = meterfsAdapter_eraseSectorCallback;
	devCtx->erase.instr.priv = (u_long)devCtx;

	devCtx->erase.res = 1;
	err = mtd_erase(&devCtx->mtd, &devCtx->erase.instr);
	if (err < 0) {
		return err;
	}
	(void)mutexLock(devCtx->erase.lock);
	while (devCtx->erase.res == 1) {
		(void)condWait(devCtx->erase.cond, devCtx->erase.lock, 0);
	}
	(void)mutexUnlock(devCtx->erase.lock);

	return devCtx->erase.res;
}


static void meterfsAdapter_powerCtrl(struct _meterfs_devCtx_t *devCtx, int state)
{
	int err;

	switch (state) {
		case 0:
			err = mtd_suspend(&devCtx->mtd);
			if ((err < 0) && (err != -EOPNOTSUPP)) {
				LOG_INFO("meterfs_mtd: Error suspending device, code: %d.", err);
			}
			break;
		case 1:
			mtd_resume(&devCtx->mtd);
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
	err = mutexCreate(&ctx->devCtx.erase.lock);
	if (err < 0) {
		free(ctx);
		return -ENOMEM;
	}
	err = condCreate(&ctx->devCtx.erase.cond);
	if (err < 0) {
		(void)resourceDestroy(ctx->devCtx.erase.lock);
		(void)resourceDestroy(ctx->lock);
		free(ctx);
		return -ENOMEM;
	}

	ctx->devCtx.mtd.name = storage->dev->mtd->name;
	ctx->devCtx.mtd.erasesize = storage->dev->mtd->erasesz;
	ctx->devCtx.mtd.writesize = storage->dev->mtd->writesz;
	ctx->devCtx.mtd.flags = MTD_WRITEABLE;
	ctx->devCtx.mtd.size = storage->size;

	ctx->devCtx.mtd.index = 0;
	ctx->devCtx.mtd.oobsize = storage->dev->mtd->oobSize;
	ctx->devCtx.mtd.oobavail = storage->dev->mtd->oobAvail;
	ctx->devCtx.mtd.storage = storage;

	ctx->meterfsCtx.sz = storage->size;
	ctx->meterfsCtx.offset = 0;
	ctx->meterfsCtx.sectorsz = storage->dev->mtd->erasesz;
	ctx->meterfsCtx.read = meterfsAdapter_read;
	ctx->meterfsCtx.write = meterfsAdapter_write;
	ctx->meterfsCtx.eraseSector = meterfsAdapter_eraseSector;
	ctx->meterfsCtx.powerCtrl = meterfsAdapter_powerCtrl;
	ctx->meterfsCtx.devCtx = &ctx->devCtx;

	err = meterfs_init(&ctx->meterfsCtx);
	if (err < 0) {
		(void)resourceDestroy(ctx->devCtx.erase.cond);
		(void)resourceDestroy(ctx->devCtx.erase.lock);
		(void)resourceDestroy(ctx->lock);
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
