/*
 * Phoenix-RTOS
 *
 * ROFS - Read Only File System in AHB address space
 *
 * Copyright 2024 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <phoenix/attribute.h>

#include "rofs.h"

#define LOG_PREFIX    "rofs: "
#define LOG(fmt, ...) printf(LOG_PREFIX fmt "\n", ##__VA_ARGS__)

#if 0
#define TRACE(fmt, ...) printf(LOG_PREFIX fmt "\n", ##__VA_ARGS__)
#else
#define TRACE(fmt, ...)
#endif


/*
 * NOTE: Implementation assumes filesystem is prepared for target native endianness
 */


#define ROFS_SIGNATURE     "ROFS"
#define ROFS_HDR_SIGNATURE 0
#define ROFS_HDR_CHECKSUM  4
#define ROFS_HDR_IMAGESIZE 8
#define ROFS_HDR_INDEXOFFS 16
#define ROFS_HDR_NODECOUNT 24


struct rofs_node {
	uint64_t timestamp;
	uint32_t parent_id;
	uint32_t id;
	uint32_t mode;
	uint32_t reserved0;
	int32_t uid;
	int32_t gid;
	uint32_t offset;
	uint32_t reserved1;
	uint32_t size;
	uint32_t reserved2;
	char name[207];
	uint8_t zero;
}; /* 256 bytes */


static uint32_t calc_crc32(const uint8_t *buf, uint32_t len, uint32_t base)
{

#if __BYTE_ORDER == __BIG_ENDIAN
#define CRC32POLY 0x04c11db7
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define CRC32POLY 0xedb88320
#else
#error "Unsupported byte order"
#endif

	uint32_t crc = base;
	int i;
	while (len--) {
		crc = (crc ^ (*buf++ & 0xff));
		for (i = 0; i < 8; i++) {
			crc = (crc >> 1) ^ ((crc & 1) ? CRC32POLY : 0);
		}
	}
	return crc;
}


static int getNode(struct rofs_ctx *ctx, oid_t *oid, struct rofs_node **retNode)
{
	if ((sizeof(oid->id) == sizeof(uint64_t)) && (oid->id >= UINT32_MAX)) {
		*retNode = NULL;
		return -ERANGE;
	}

	if ((oid->port != ctx->oid.port) || ((uint32_t)oid->id >= ctx->nodeCount)) {
		*retNode = NULL;
		return -ENOENT;
	}

	if (ctx->tree[oid->id].zero != 0) {
		*retNode = NULL;
		return -EBADF;
	}

	*retNode = &ctx->tree[oid->id];
	return 0;
}


int rofs_init(struct rofs_ctx *ctx, unsigned long imageAddr)
{
	uint8_t *imagePtr;
	uint32_t crc = ~0;
	uint32_t indexOffs;

	if ((imageAddr & (_PAGE_SIZE - 1)) != 0) {
		LOG("Image address needs to be aligned to PAGE_SIZE");
		return -EINVAL;
	}

	/* Temporarily map PAGE_SIZE to read ROFS header */
	imagePtr = mmap(NULL, _PAGE_SIZE, PROT_READ, MAP_PHYSMEM | MAP_ANONYMOUS, -1, imageAddr);
	if (imagePtr == MAP_FAILED) {
		return -ENODEV;
	}

	/* Check image signature */
	if (memcmp((imagePtr + ROFS_HDR_SIGNATURE), ROFS_SIGNATURE, sizeof(ROFS_SIGNATURE) - 1) != 0) {
		return -EINVAL;
	}

	ctx->checksum = *(uint32_t *)(imagePtr + ROFS_HDR_CHECKSUM);
	ctx->imgSize = *(uint32_t *)(imagePtr + ROFS_HDR_IMAGESIZE);
	ctx->nodeCount = *(uint32_t *)(imagePtr + ROFS_HDR_NODECOUNT);
	indexOffs = *(uint32_t *)(imagePtr + ROFS_HDR_INDEXOFFS);

	munmap(imagePtr, _PAGE_SIZE);

	if ((indexOffs & (sizeof(uint64_t) - 1)) != 0) {
		LOG("Image index offset is invalid");
		return -EINVAL;
	}

	/* Map whole image */
	ctx->imgAlignedSize = ((ctx->imgSize + _PAGE_SIZE - 1) & ~(_PAGE_SIZE - 1));
	imagePtr = mmap(NULL, ctx->imgAlignedSize, PROT_READ, MAP_PHYSMEM | MAP_ANONYMOUS, -1, imageAddr);
	if (imagePtr == MAP_FAILED) {
		return -ENODEV;
	}

	crc = ~calc_crc32((uint8_t *)(imagePtr + ROFS_HDR_IMAGESIZE), ctx->imgSize - ROFS_HDR_IMAGESIZE, crc);

	if (crc != ctx->checksum) {
		LOG("invalid crc %08X vs %08X", crc, ctx->checksum);
		munmap(imagePtr, ctx->imgAlignedSize);
		return -EINVAL;
	}

	ctx->tree = (struct rofs_node *)(imagePtr + indexOffs);
	ctx->imgPtr = imagePtr;

	LOG("image=0x%p, nodes=%d", imagePtr, ctx->nodeCount);

	return 0;
}


void rofs_setdev(struct rofs_ctx *ctx, oid_t *oid)
{
	ctx->oid = *oid;
}


int rofs_open(struct rofs_ctx *ctx, oid_t *oid)
{
	struct rofs_node *node;
	int ret = getNode(ctx, oid, &node);
	TRACE("open id=%ju ret=%d", (uintmax_t)oid->id, ret);
	return ret;
}


int rofs_close(struct rofs_ctx *ctx, oid_t *oid)
{
	struct rofs_node *node;
	int ret = getNode(ctx, oid, &node);
	TRACE("close id=%ju ret=%d", (uintmax_t)oid->id, ret);
	return ret;
}


int rofs_read(struct rofs_ctx *ctx, oid_t *oid, off_t offs, char *buff, size_t len)
{
	struct rofs_node *node;
	int ret = getNode(ctx, oid, &node);

	TRACE("read id=%ju, of=%jd, buf=0x%p, len=%zu, ret=%d", (uintmax_t)oid->id, (intmax_t)offs, buff, len, ret);

	if (ret != 0) {
		return ret;
	}

	if ((offs >= (off_t)node->size) || (offs < 0)) {
		return 0;
	}

	if ((size_t)offs + len > (size_t)node->size) {
		len = (size_t)node->size - offs;
	}

	memcpy(buff, (uint8_t *)ctx->imgPtr + node->offset + offs, len);
	return len;
}


int rofs_write(struct rofs_ctx *ctx, oid_t *oid, off_t offs, const char *buff, size_t len)
{
	(void)ctx;
	TRACE("write id=%ju, of=%jd, buf=0x%p, len=%zu", (uintmax_t)oid->id, (intmax_t)offs, buff, len);
	return -ENOSYS;
}


int rofs_truncate(struct rofs_ctx *ctx, oid_t *oid, size_t size)
{
	(void)ctx;
	TRACE("truncate id=%ju, size=%zu", (uintmax_t)oid->id, size);
	return -ENOSYS;
}


int rofs_create(struct rofs_ctx *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	(void)ctx;
	TRACE("create dir=%p, name=%s, oid=%p, mode=%x, type=%d, dev=%p", dir, name, oid, mode, type, dev);
	return -ENOSYS;
}


int rofs_destroy(struct rofs_ctx *ctx, oid_t *oid)
{
	(void)ctx;
	TRACE("destroy id=%jd", (uintmax_t)oid->id);
	return -ENOSYS;
}


int rofs_setattr(struct rofs_ctx *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size)
{
	(void)ctx;
	TRACE("setattr id=%ju, type=%d, attr=%llx", (uintmax_t)oid->id, type, attr);
	return -ENOSYS;
}


int rofs_getattr(struct rofs_ctx *ctx, oid_t *oid, int type, long long *attr)
{
	int ret = 0;
	struct rofs_node *node;

	TRACE("getattr id=%ju, type=%d, attr=0x%llx", (uintmax_t)oid->id, type, attr ? *attr : -1);

	if (oid->id >= ctx->nodeCount) {
		return -EPIPE;
	}

	node = &ctx->tree[oid->id];

	switch (type) {
		case atMode:
			*attr = node->mode;
			break;

		case atUid:
			*attr = node->uid;
			break;

		case atGid:
			*attr = node->gid;
			break;

		case atSize:
			*attr = node->size;
			break;

		case atBlocks:
			*attr = (node->size + S_BLKSIZE - 1) / S_BLKSIZE;
			break;

		case atIOBlock:
			*attr = S_BLKSIZE;
			break;

		case atType:
			if (S_ISDIR(node->mode)) {
				*attr = otDir;
			}
			else if (S_ISREG(node->mode)) {
				*attr = otFile;
			}
			else {
				*attr = otUnknown;
			}
			break;

		case atPollStatus:
			*attr = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
			break;

		case atCTime:
			*attr = node->timestamp;
			break;

		case atMTime:
			*attr = node->timestamp;
			break;

		case atATime:
			*attr = node->timestamp;
			break;

		case atLinks:
			*attr = 0;
			break;

		default:
			ret = -EINVAL;
			break;
	}


	return ret;
}


int rofs_getattrall(struct rofs_ctx *ctx, oid_t *oid, struct _attrAll *attrs, size_t attrSize)
{
	struct rofs_node *node;

	TRACE("getattrall id=%ju, attr=0x%p sz=%zu", (uintmax_t)oid->id, attrs, attrSize);

	if ((attrs == NULL) || (attrSize < sizeof(struct _attrAll))) {
		return -EINVAL;
	}

	if (oid->id >= ctx->nodeCount) {
		return -EBADF;
	}

	node = &ctx->tree[oid->id];

	_phoenix_initAttrsStruct(attrs, -ENOSYS);
	attrs->size.val = node->size;
	attrs->size.err = 0;

	attrs->mode.val = node->mode;
	attrs->mode.err = 0;

	if (S_ISDIR(node->mode)) {
		attrs->type.val = otDir;
	}
	else if (S_ISREG(node->mode)) {
		attrs->type.val = otFile;
	}
	else {
		attrs->type.val = otUnknown;
	}

	attrs->uid.val = node->uid;
	attrs->uid.err = 0;

	attrs->gid.val = node->gid;
	attrs->gid.err = 0;

	attrs->blocks.val = (node->size + S_BLKSIZE - 1) / S_BLKSIZE;
	attrs->blocks.err = 0;

	attrs->ioblock.val = S_BLKSIZE;
	attrs->ioblock.err = 0;

	attrs->pollStatus.val = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
	attrs->pollStatus.err = 0;

	attrs->cTime.val = node->timestamp;
	attrs->cTime.err = 0;

	attrs->mTime.val = node->timestamp;
	attrs->mTime.err = 0;

	attrs->aTime.val = node->timestamp;
	attrs->aTime.err = 0;

	attrs->links.val = 0;
	attrs->links.err = 0;

	return 0;
}


static int dirfind(struct rofs_ctx *ctx, struct rofs_node **pNode, int parent_id, const char *name, oid_t *o)
{
	struct rofs_node *node;
	uint32_t i;
	int len;

	if ((name == NULL) || (name[0] == '\0')) {
		return -ENOENT;
	}

	len = 0;
	while ((name[len] != '/') && (name[len] != '\0')) {
		len++;
	}

	for (i = 0; i < ctx->nodeCount; i++) {
		node = &ctx->tree[i];
		if ((node->parent_id == parent_id) && (strlen(node->name) == len) && (strncmp(name, node->name, len) == 0)) {
			o->id = node->id;
			o->port = ctx->oid.port;
			*pNode = node;
			return len;
		}
	}

	return -ENOENT;
}


int rofs_lookup(struct rofs_ctx *ctx, oid_t *dir, const char *name, oid_t *fil, oid_t *dev)
{
	TRACE("lookup name='%s' oid=%u.%ju port=%d", name, dir->port, (uintmax_t)dir->id, ctx->oid.port);

	struct rofs_node *node = NULL;
	int parent_id = 0;
	int len = 0;
	int res = -ENOENT;

	fil->port = ctx->oid.port;
	if (name == NULL) {
		return -EINVAL;
	}

	if ((dir != NULL) && (dir->port == ctx->oid.port)) {
		parent_id = dir->id;
	}

	while (name[len] != '\0') {
		while (name[len] == '/') {
			len++;
		}

		res = dirfind(ctx, &node, parent_id, &name[len], fil);
		if (res <= 0) {
			break;
		}
		else {
			len += res;
		}
		parent_id = node->id;
	}

	if (res < 0) {
		return res;
	}

	*dev = *fil;

	return len;
}


int rofs_link(struct rofs_ctx *ctx, oid_t *dir, const char *name, oid_t *oid)
{
	(void)ctx;
	TRACE("link dir=%ju, name=%s, oid=%p", (uintmax_t)dir->id, name, oid);
	return -ENOSYS;
}


int rofs_unlink(struct rofs_ctx *ctx, oid_t *dir, const char *name)
{
	(void)ctx;
	TRACE("unlink dir=%ju, name=%s", (uintmax_t)dir->id, name);
	return -ENOSYS;
}


int rofs_readdir(struct rofs_ctx *ctx, oid_t *dir, off_t offs, struct dirent *dent, size_t size)
{
	TRACE("readdir id=%ju, of=%jd, dent=0x%p, size=%zu", (uintmax_t)dir->id, (intmax_t)offs, dent, size);

	struct rofs_node *node;
	uint32_t i, count = 2;

	int ret = getNode(ctx, dir, &node);
	if (ret != 0) {
		return ret;
	}

	if (count > offs) {
		strcpy(dent->d_name, (offs == 0) ? "." : "..");
		dent->d_ino = (offs == 0) ? node->id : node->parent_id;
		dent->d_namlen = (offs == 0) ? 1 : 2;
		dent->d_reclen = 1;
		dent->d_type = DT_DIR;
		return 0;
	}

	for (i = 0; i < ctx->nodeCount; i++) {
		node = &ctx->tree[i];
		if (node->parent_id == dir->id) {
			if (count++ < offs) {
				continue;
			}

			strcpy(dent->d_name, node->name);
			dent->d_ino = node->id;
			dent->d_reclen = 1;
			dent->d_namlen = strlen(node->name);
			dent->d_type = S_ISDIR(node->mode) ? DT_DIR : DT_REG;
			return 0;
		}
	}

	return -ENOENT;
}


int rofs_createMapped(struct rofs_ctx *ctx, oid_t *dir, const char *name, void *addr, size_t size, oid_t *oid)
{
	(void)ctx;
	TRACE("createMapped dir=%p, name=%s, addr=%p, size=%zu, oid=0x%p", dir, name, addr, size, oid);
	return -ENOSYS;
}


int rofs_statfs(struct rofs_ctx *ctx, void *buf, size_t len)
{
	(void)ctx;
	TRACE("statfs buf=0x%p, len=%zu", buf, len);
	return -ENOSYS;
}


int rofs_devctl(struct rofs_ctx *ctx, msg_t *msg)
{
	(void)ctx;
	TRACE("devctl msg=0x%p", msg);
	return -ENOSYS;
}


int rofs_mount(struct rofs_ctx *ctx, oid_t *oid, mount_i_msg_t *imnt, mount_o_msg_t *omnt)
{
	(void)ctx;
	TRACE("mount");
	omnt->oid = *oid;
	return 0;
}


int rofs_unmount(struct rofs_ctx *ctx)
{
	(void)ctx;
	TRACE("umount");
	return -ENOSYS;
}
