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
#include <sys/minmax.h>
#include <sys/mman.h>
#include <phoenix/attribute.h>

#include <tinyaes/aes.h>
#include <tinyaes/cmac.h>

#include "rofs.h"

#define LOG_PREFIX    "rofs: "
#define LOG(fmt, ...) printf(LOG_PREFIX fmt "\n", ##__VA_ARGS__)

#ifndef ROFS_DEBUG
#define ROFS_DEBUG 0
#endif

#if ROFS_DEBUG
#define TRACE(fmt, ...) printf(LOG_PREFIX fmt "\n", ##__VA_ARGS__)
#else
#define TRACE(fmt, ...)
#endif


/*
 * NOTE: Implementation assumes filesystem is prepared for target native endianness
 */


#define ROFS_SIGNATURE      "ROFS"
#define ROFS_HDR_SIGNATURE  0
#define ROFS_HDR_CHECKSUM   4
#define ROFS_HDR_IMAGESIZE  8
#define ROFS_HDR_INDEXOFFS  16
#define ROFS_HDR_NODECOUNT  24
#define ROFS_HDR_ENCRYPTION 32
#define ROFS_HDR_CRYPT_SIG  34 /* let CRYPT_SIG to be at least 64-byte long to allow for future signatures schemes (e.g. ed25519) */
#define ROFS_HEADER_SIZE    128

_Static_assert(AES_BLOCKLEN <= ROFS_HEADER_SIZE - ROFS_HDR_CRYPT_SIG, "AES_MAC does not fit into the rofs header");

/* clang-format off */
enum { encryption_none, encryption_aes };
/* clang-format on */


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
} __attribute__((packed)); /* 256 bytes */

_Static_assert(ROFS_BUFSZ >= sizeof(struct rofs_node), "buffer must fit rofs_node");


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


static int calc_AESCMAC(struct rofs_ctx *ctx, uint32_t ofs, uint32_t len, uint8_t mac[AES_BLOCKLEN])
{
	size_t todo = len;
	struct CMAC_ctx cmacCtx;

	CMAC_init_ctx(&cmacCtx, ctx->key);

	while (todo > 0) {
		size_t chunksz = (todo > sizeof(ctx->buf)) ? sizeof(ctx->buf) : todo;
		ssize_t rlen = ctx->devRead(ctx, ctx->buf, chunksz, ofs + len - todo);

		if (chunksz != rlen) {
			LOG("devRead failed: %zd", rlen);
			return -1;
		}

		CMAC_append(&cmacCtx, ctx->buf, rlen);
		todo -= rlen;
	}

	CMAC_calculate(&cmacCtx, mac);

	return 0;
}


static uint8_t *ivSerializeU32(uint8_t *buff, uint32_t d)
{
	buff[0] = (d >> 0) & 0xff;
	buff[1] = (d >> 8) & 0xff;
	buff[2] = (d >> 16) & 0xff;
	buff[3] = (d >> 24) & 0xff;

	return &buff[4];
}


static inline void constructIV(uint8_t *iv, const struct rofs_node *node)
{
	/* TODO: ESSIV? */
	uint8_t *tbuff = ivSerializeU32(iv, node->id);
	tbuff = ivSerializeU32(tbuff, node->offset);
	tbuff = ivSerializeU32(tbuff, node->uid);
	uint32_t t = 0;
	ivSerializeU32(tbuff, t);
}


static void xcrypt(void *buff, size_t bufflen, const uint8_t *key, const struct rofs_node *node)
{
	struct AES_ctx ctx;
	uint8_t iv[AES_BLOCKLEN];

	constructIV(iv, node);
	AES_init_ctx_iv(&ctx, key, iv);
	AES_CTR_xcrypt_buffer(&ctx, buff, bufflen);
}


/* WARN: returned pointer valid until next nodeFromTree call */
static struct rofs_node *nodeFromTree(struct rofs_ctx *ctx, int id)
{
	int ret;
	uint32_t ofs = ctx->indexOffs + id * sizeof(struct rofs_node);

	TRACE("nodeFromTree id=%d, ofs=%u", id, ofs);

	if (ctx->tree != NULL) {
		return &ctx->tree[id];
	}

	if (ofs >= ctx->imgSize) {
		return NULL;
	}

	ret = ctx->devRead(ctx, ctx->buf, sizeof(struct rofs_node), ofs);
	if (ret != sizeof(struct rofs_node)) {
		return NULL;
	}

	TRACE("nodeFromTree res: id=%d, ofs=%u name=%s", id, ofs, ((struct rofs_node *)ctx->buf)->name);

	return (struct rofs_node *)ctx->buf;
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

	struct rofs_node *node = nodeFromTree(ctx, oid->id);

	if (node == NULL) {
		*retNode = NULL;
		return -EINVAL;
	}

	if (node->zero != 0) {
		*retNode = NULL;
		return -EBADF;
	}

	*retNode = node;

	return 0;
}


int rofs_init(struct rofs_ctx *ctx, rofs_devRead_t devRead, unsigned long imageAddr, const uint8_t *key)
{
	uint8_t *imagePtr = NULL;
	uint32_t crc = ~0;
	int ret;

	ctx->tree = NULL;
	ctx->imgPtr = NULL;
	ctx->devRead = devRead;

	if (imageAddr != 0) {
		if ((imageAddr & (_PAGE_SIZE - 1)) != 0) {
			LOG("Image address needs to be aligned to PAGE_SIZE");
			return -EINVAL;
		}

		/* Temporarily map PAGE_SIZE to read ROFS header */
		imagePtr = mmap(NULL, _PAGE_SIZE, PROT_READ, MAP_PHYSMEM | MAP_ANONYMOUS, -1, imageAddr);
		if (imagePtr == MAP_FAILED) {
			return -ENODEV;
		}

		ctx->imgPtr = imagePtr;
	}

	ret = ctx->devRead(ctx, ctx->buf, ROFS_HEADER_SIZE, 0);
	if (ret != ROFS_HEADER_SIZE) {
		ret = -EIO;
		/* defer return on failure to after munmap in case imageAddr != 0 */
	}

	if (imageAddr != 0) {
		if (munmap(imagePtr, _PAGE_SIZE) < 0) {
			LOG("munmap failed: %d", errno);
			return -errno;
		}
		ctx->imgPtr = NULL;
	}

	if (ret < 0) {
		return ret;
	}

	/* Check image signature */
	if (memcmp((ctx->buf + ROFS_HDR_SIGNATURE), ROFS_SIGNATURE, sizeof(ROFS_SIGNATURE) - 1) != 0) {
		return -EINVAL;
	}

	ctx->checksum = *(uint32_t *)(ctx->buf + ROFS_HDR_CHECKSUM);
	ctx->imgSize = *(uint32_t *)(ctx->buf + ROFS_HDR_IMAGESIZE);
	ctx->nodeCount = *(uint32_t *)(ctx->buf + ROFS_HDR_NODECOUNT);
	ctx->indexOffs = *(uint32_t *)(ctx->buf + ROFS_HDR_INDEXOFFS);
	ctx->encryption = *(uint16_t *)(ctx->buf + ROFS_HDR_ENCRYPTION);

	uint8_t target_mac[AES_BLOCKLEN];
	memcpy(target_mac, ctx->buf + ROFS_HDR_CRYPT_SIG, AES_BLOCKLEN);

	if ((ctx->indexOffs & (sizeof(uint64_t) - 1)) != 0) {
		LOG("Image index offset is invalid");
		return -EINVAL;
	}

#if ROFS_DEBUG
	char buf[2 * AES_BLOCKLEN + 1];
	size_t ofs = 0;
	for (int i = 0; i < AES_BLOCKLEN; i++) {
		ofs += snprintf(buf + ofs, 2 * AES_BLOCKLEN - ofs, "%.2x", target_mac[i]);
	}
	buf[ofs] = '\0';
	TRACE("target AES-CMAC: %s", buf);
#endif

	if (imageAddr != 0) {
		/* Map whole image */
		ctx->imgAlignedSize = ((ctx->imgSize + _PAGE_SIZE - 1) & ~(_PAGE_SIZE - 1));
		imagePtr = mmap(NULL, ctx->imgAlignedSize, PROT_READ, MAP_PHYSMEM | MAP_ANONYMOUS, -1, imageAddr);
		if (imagePtr == MAP_FAILED) {
			return -ENODEV;
		}
		ctx->imgPtr = imagePtr;
		crc = ~calc_crc32((uint8_t *)(imagePtr + ROFS_HDR_IMAGESIZE), ctx->imgSize - ROFS_HDR_IMAGESIZE, crc);
	}
	else {
		uint32_t ofs = ROFS_HDR_IMAGESIZE, len = 0;

		while (ofs < ctx->imgSize) {
			len = min(ROFS_BUFSZ, ctx->imgSize - ofs);

			ret = ctx->devRead(ctx, ctx->buf, len, ofs);
			if (ret != len) {
				LOG("devRead failed: %d", ret);
				return -EIO;
			}

			crc = calc_crc32(ctx->buf, len, crc);
			ofs += len;
		}

		crc = ~crc;
	}

	do {
		if (crc != ctx->checksum) {
			LOG("invalid crc %08X vs %08X", crc, ctx->checksum);
			ret = -EINVAL;
			break;
		}

		TRACE("SIG OK: crc32=%08X imgSize=%zu nodes=%d", crc, ctx->imgSize, ctx->nodeCount);

		if (ctx->imgPtr != NULL) {
			ctx->tree = (struct rofs_node *)(ctx->imgPtr + ctx->indexOffs);
		}

		if (key != NULL) {
			if (ctx->encryption != encryption_aes) {
				LOG("image encryption type mismatch: %d != %d", ctx->encryption, encryption_aes);
				ret = -EINVAL;
				break;
			}

			ctx->key = key;
			uint8_t actual_mac[AES_BLOCKLEN];
			ret = calc_AESCMAC(ctx, ROFS_HEADER_SIZE, ctx->imgSize - ROFS_HEADER_SIZE, actual_mac);
			if (ret < 0) {
				LOG("failed to calculate AES-CMAC: %d", ret);
				ret = -EIO;
				break;
			}

			if (memcmp(actual_mac, target_mac, AES_BLOCKLEN) != 0) {
				LOG("AES-CMAC mismatch");
				ret = -EINVAL;
				break;
			}
		}
	} while (0);

	if (ret < 0) {
		if (ctx->imgPtr != NULL) {
			munmap(ctx->imgPtr, ctx->imgAlignedSize);
		}
		return ret;
	}

	return 0;
}


void rofs_setdev(struct rofs_ctx *ctx, oid_t *oid)
{
	ctx->oid = *oid;
}


oid_t rofs_getdev(struct rofs_ctx *ctx)
{
	return ctx->oid;
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

	ret = ctx->devRead(ctx, buff, len, node->offset + offs);
	if (ret < 0) {
		return ret;
	}

	if (ctx->key != NULL) {
		xcrypt(buff, (size_t)ret, ctx->key, node);
	}

	return ret;
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

	node = nodeFromTree(ctx, oid->id);
	if (node == NULL) {
		return -EINVAL;
	}

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

	node = nodeFromTree(ctx, oid->id);

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
		node = nodeFromTree(ctx, i);
		if (node == NULL) {
			return -EIO;
		}

		if ((node->parent_id == parent_id) && (strnlen(node->name, len + 1) == len) && (strncmp(name, node->name, len) == 0)) {
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
		node = nodeFromTree(ctx, i);
		if (node == NULL) {
			return -EIO;
		}

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


void *rofs_getImgPtr(struct rofs_ctx *ctx)
{
	return ctx->imgPtr;
}
