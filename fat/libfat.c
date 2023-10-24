/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * libstorage interface for filesystem operations
 *
 * Copyright 2023 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "libfat.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/threads.h>

#include "fatio.h"
#include "fatchain.h"
#include "fatdev.h"

#define LOG_TAG "libfat"
/* clang-format off */
#define LOG_ERROR(str, ...) do { fprintf(stderr, LOG_TAG " error: " str "\n", ##__VA_ARGS__); } while (0)
#define TRACE(str, ...)     do { if (FATFS_DEBUG) fprintf(stderr, LOG_TAG " trace: " str "\n", ##__VA_ARGS__); } while (0)
/* clang-format on */

#define DEFAULT_PERMISSIONS (S_IRUSR | S_IRGRP | S_IROTH)

typedef struct {
	rbnode_t node;

	fat_fileID_t id;
	size_t refcount;
	uint32_t size;
	handle_t lock;
	fatchain_cache_t chain;
	bool isDir;
} fat_obj_t;


static ssize_t getDirentById(fat_info_t *info, id_t id, fat_dirent_t *d)
{
	fat_fileID_t fatID;
	fatID.raw = id;

	if (fatID.raw == FAT_ROOT_ID) {
		memset(d, 0, sizeof(*d));
		fat_setCluster(d, ROOT_DIR_CLUSTER);
		d->attr = FAT_ATTR_DIRECTORY;
		return sizeof(*d);
	}

	fatchain_cache_t c;
	fatchain_initCache(&c, fatID.dirCluster);
	return fatio_read(info, &c, fatID.offsetInDir, sizeof(fat_dirent_t), d);
}


static int libfat_objCmp(rbnode_t *node1, rbnode_t *node2)
{
	fat_obj_t *obj1 = lib_treeof(fat_obj_t, node, node1);
	fat_obj_t *obj2 = lib_treeof(fat_obj_t, node, node2);

	if (obj1->id.raw > obj2->id.raw) {
		return 1;
	}
	else if (obj1->id.raw < obj2->id.raw) {
		return -1;
	}

	return 0;
}


static fat_obj_t *libfat_findObj(fat_info_t *info, id_t id)
{
	fat_obj_t tmp;
	tmp.id.raw = id;
	return lib_treeof(fat_obj_t, node, lib_rbFind(&info->openObjs, &tmp.node));
}


ssize_t libfat_write(void *info, oid_t *oid, offs_t offs, const void *data, size_t len)
{
	return -EROFS;
}


static int libfat_setattr(void *info, oid_t *oid, int type, long long attr, void *data, size_t len)
{
	return -EROFS;
}


static int libfat_truncate(void *info, oid_t *oid, size_t size)
{
	return -EROFS;
}


static int libfat_create(void *info, oid_t *oid, const char *name, oid_t *dev, unsigned int mode, int type, oid_t *res)
{
	return -EROFS;
}


static int libfat_destroy(void *info, oid_t *oid)
{
	return -EROFS;
}


static int libfat_lookup(void *infoVoid, oid_t *oid, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	if (infoVoid == NULL) {
		return -EINVAL;
	}

	fat_info_t *info = infoVoid;

	fat_fileID_t fileID;
	fat_dirent_t d;
	fileID.raw = oid->id;
	getDirentById(info, oid->id, &d);
	int ret = fatio_lookupUntilEnd(info, name, &d, &fileID);
	if (ret < 0) {
		TRACE("lookup failed %d", ret);
		return ret;
	}

	res->port = info->port;
	res->id = fileID.raw;
	/* Always the same because there are no device files on FAT disks */
	*dev = *res;
	return strlen(name);
}


static int libfat_getattr(void *infoVoid, oid_t *oid, int type, long long *attr)
{
	if (infoVoid == NULL) {
		return -EINVAL;
	}

	fat_info_t *info = infoVoid;
	fat_dirent_t d;
	int ret = getDirentById(info, oid->id, &d);
	if (ret < 0) {
		TRACE("getattr failed %d", ret);
		return ret;
	}

	size_t clusterSize = info->bsbpb.BPB_BytesPerSec * info->bsbpb.BPB_SecPerClus;
	switch (type) {
		case atMode:
			*attr = info->fsPermissions | (fat_isDirectory(&d) ? S_IFDIR : S_IFREG);
			break;

		case atUid:
			*attr = 0;
			break;

		case atGid:
			*attr = 0;
			break;

		case atSize:
			*attr = d.size;
			break;

		case atBlocks:
			*attr = (d.size + clusterSize - 1) / clusterSize;
			break;

		case atIOBlock:
			*attr = clusterSize;
			break;

		case atType:
			*attr = fat_isDirectory(&d) ? otDir : otFile;
			break;

		case atCTime:
			*attr = fatdir_getFileTime(&d, FAT_FILE_CTIME);
			break;

		case atATime:
			*attr = fatdir_getFileTime(&d, FAT_FILE_ATIME);
			break;

		case atMTime:
			*attr = fatdir_getFileTime(&d, FAT_FILE_MTIME);
			break;

		case atLinks:
			*attr = 1;
			break;

		case atPollStatus:
			*attr = 0;
			break;

		default:
			return -EINVAL;
	}

	return EOK;
}


static int libfat_open(void *infoVoid, oid_t *oid)
{
	if (infoVoid == NULL) {
		return -EINVAL;
	}

	fat_info_t *info = infoVoid;
	fat_dirent_t d;
	mutexLock(info->objLock);
	fat_obj_t *obj = libfat_findObj(info, oid->id);
	if (obj != NULL) {
		mutexLock(obj->lock);
		mutexUnlock(info->objLock);
		obj->refcount++;
		mutexUnlock(obj->lock);
		return EOK;
	}

	int ret = getDirentById(info, oid->id, &d);
	if (ret < 0) {
		return ret;
	}

	obj = (fat_obj_t *)malloc(sizeof(fat_obj_t));
	if (obj == NULL) {
		mutexUnlock(info->objLock);
		return -ENOMEM;
	}

	if (mutexCreate(&obj->lock) < 0) {
		free(obj);
		mutexUnlock(info->objLock);
		return -ENOMEM;
	}

	fatchain_initCache(&obj->chain, fat_getCluster(&d, info->type));
	obj->isDir = fat_isDirectory(&d);
	obj->id.raw = oid->id;
	obj->refcount = 1;
	obj->size = d.size;

	lib_rbInsert(&info->openObjs, &obj->node);
	mutexUnlock(info->objLock);
	return EOK;
}


static int libfat_close(void *infoVoid, oid_t *oid)
{
	if (infoVoid == NULL) {
		return -EINVAL;
	}

	fat_info_t *info = infoVoid;
	mutexLock(info->objLock);
	fat_obj_t *obj = libfat_findObj(info, oid->id);
	if (obj == NULL) {
		mutexUnlock(info->objLock);
		return -EINVAL;
	}

	mutexLock(obj->lock);
	obj->refcount--;
	if (obj->refcount > 0) {
		mutexUnlock(info->objLock);
		mutexUnlock(obj->lock);
		return EOK;
	}

	lib_rbRemove(&info->openObjs, &obj->node);
	mutexUnlock(obj->lock);
	resourceDestroy(obj->lock);
	free(obj);
	mutexUnlock(info->objLock);
	return EOK;
}


ssize_t libfat_read(void *infoVoid, oid_t *oid, offs_t offs, void *data, size_t len)
{
	if (infoVoid == NULL) {
		return -EINVAL;
	}

	fat_info_t *info = infoVoid;
	mutexLock(info->objLock);
	fat_obj_t *obj = libfat_findObj(info, oid->id);
	if (obj == NULL) {
		mutexUnlock(info->objLock);
		return -EINVAL;
	}

	mutexLock(obj->lock);
	mutexUnlock(info->objLock);
	ssize_t ret;
	if (offs >= obj->size) {
		ret = 0;
	}
	else {
		len = min(obj->size - offs, len);
		ret = fatio_read(info, &obj->chain, offs, len, data);
	}

	mutexUnlock(obj->lock);
	return ret;
}


typedef struct {
	struct dirent *dent;
	size_t dentSize;
	uint32_t startOffset;
} libfat_readdirCbArg_t;


static int libfat_readdirCb(void *argVoid, fat_dirent_t *d, fat_name_t *name, uint32_t offsetInDir)
{
	if (d == NULL) {
		return -ENOENT;
	}

	if (fat_isDeleted(d)) {
		return 0;
	}

	libfat_readdirCbArg_t *arg = argVoid;
	size_t maxNameLength = arg->dentSize - sizeof(struct dirent);
	ssize_t realNameLength = fatdir_nameToUTF8(name, arg->dent->d_name, maxNameLength);
	if (realNameLength < 0) {
		return -EINVAL;
	}

	size_t outputNameLength = min(maxNameLength, realNameLength);
	if (outputNameLength != realNameLength) {
		LOG_ERROR("Name truncated: got %u need %u\n", (uint32_t)maxNameLength, (uint32_t)realNameLength);
	}

	arg->dent->d_namlen = outputNameLength;
	arg->dent->d_type = fat_isDirectory(d) ? DT_DIR : DT_REG;
	arg->dent->d_reclen = offsetInDir - arg->startOffset + sizeof(fat_dirent_t);
	arg->dent->d_ino = 0;
	return -EEXIST;
}


static int libfat_readdir(void *infoVoid, oid_t *oid, offs_t offs, struct dirent *dent, size_t size)
{
	if (infoVoid == NULL) {
		return -EINVAL;
	}

	if ((dent == NULL) || ((offs % sizeof(fat_dirent_t)) != 0)) {
		return -EINVAL;
	}

	fat_info_t *info = infoVoid;
	mutexLock(info->objLock);
	fat_obj_t *obj = libfat_findObj(info, oid->id);
	if (obj == NULL) {
		mutexUnlock(info->objLock);
		return -EINVAL;
	}

	if (!obj->isDir) {
		mutexUnlock(info->objLock);
		return -ENOTDIR;
	}

	mutexLock(obj->lock);
	mutexUnlock(info->objLock);
	libfat_readdirCbArg_t arg;
	arg.dent = dent;
	arg.dentSize = size;
	arg.startOffset = offs;

	int ret = fatio_dirScan(info, &obj->chain, offs, libfat_readdirCb, &arg);
	mutexUnlock(obj->lock);
	if (ret == -EEXIST) {
		return dent->d_reclen;
	}

	return ret;
}


static int libfat_statfs(void *infoVoid, void *buf, size_t len)
{
	fat_info_t *info = infoVoid;
	if (info == NULL) {
		return -EINVAL;
	}

	struct statvfs *st = buf;
	if ((st == NULL) || (len != sizeof(*st))) {
		return -EINVAL;
	}

	fat_cluster_t freeClusters = fatchain_scanFreeSpace(info);
	size_t clusterSize = info->bsbpb.BPB_SecPerClus * info->bsbpb.BPB_BytesPerSec;
	st->f_bsize = st->f_frsize = clusterSize;
	st->f_blocks = info->dataClusters;
	st->f_bavail = st->f_bfree = freeClusters;
	/* TODO: counting files requires recursively scanning all directories */
	st->f_files = 0;
	/* This is not accurate, but this field doesn't make sense for FAT anyway */
	st->f_favail = st->f_ffree = (fsfilcnt_t)freeClusters * (clusterSize / sizeof(fat_dirent_t));
	st->f_fsid = info->bsbpb.BS_VolID;
	st->f_flag = ST_RDONLY;
	/* Decoding UTF-16 into UTF-8 creates up to 3x more characters */
	st->f_namemax = FAT_MAX_NAMELEN * 3;

	return EOK;
}


const static storage_fsops_t fsOps = {
	.open = libfat_open,
	.close = libfat_close,
	.read = libfat_read,
	.write = libfat_write,     /* Only returns -EROFS */
	.setattr = libfat_setattr, /* Only returns -EROFS */
	.getattr = libfat_getattr,
	.truncate = libfat_truncate, /* Only returns -EROFS */
	.devctl = NULL,
	.create = libfat_create,   /* Only returns -EROFS */
	.destroy = libfat_destroy, /* Only returns -EROFS */
	.lookup = libfat_lookup,
	.link = NULL,
	.unlink = NULL,
	.readdir = libfat_readdir,
	.statfs = libfat_statfs,
	.sync = NULL
};


int libfat_umount(storage_fs_t *strg_fs)
{
	fat_info_t *info = strg_fs->info;
	mutexLock(info->objLock);
	rbnode_t *node = lib_rbMinimum(info->openObjs.root);
	while (node != NULL) {
		rbnode_t *next = lib_rbNext(node);
		fat_obj_t *obj = lib_treeof(fat_obj_t, node, node);
		handle_t lockTmp = obj->lock;
		resourceDestroy(lockTmp);
		free(obj);
		node = next;
	}

	mutexUnlock(info->objLock);
	resourceDestroy(info->objLock);
	free(info);
	return EOK;
}


int libfat_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root)
{
	if (sizeof(id_t) < sizeof(fat_fileID_t)) {
		/* Current implementation assumes that one will fit into the other */
		return -EOPNOTSUPP;
	}

	int err;
	if (strg == NULL ||
		strg->dev == NULL ||
		strg->dev->blk == NULL ||
		strg->dev->blk->ops == NULL ||
		strg->dev->blk->ops->read == NULL) {
		return -ENOSYS;
	}

	fat_info_t *info = malloc(sizeof(fat_info_t));
	if (info == NULL) {
		return -ENOMEM;
	}

	if (mutexCreate(&info->objLock) < 0) {
		free(info);
		return -ENOMEM;
	}

	info->strg = strg;
	info->port = root->port;
	info->fsPermissions = (mode == 0) ? DEFAULT_PERMISSIONS : (mode & ACCESSPERMS);

	err = fat_readFilesystemInfo(info);
	if (err < 0) {
		resourceDestroy(info->objLock);
		free(info);
		return err;
	}

	lib_rbInit(&info->openObjs, libfat_objCmp, NULL);

	fs->info = info;
	fs->ops = &fsOps;

	root->id = FAT_ROOT_ID;
	return EOK;
}
