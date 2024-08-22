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

#ifndef ROFS_H
#define ROFS_H

#include <stdint.h>
#include <sys/types.h>


struct rofs_ctx {
	void *imgPtr;
	size_t imgSize;
	size_t imgAlignedSize;
	uint32_t checksum;
	struct rofs_node *tree;
	uint32_t nodeCount;
	oid_t oid;
};


int rofs_init(struct rofs_ctx *ctx, unsigned long imgAddr);


void rofs_setdev(struct rofs_ctx *ctx, oid_t *oid);


int rofs_open(struct rofs_ctx *ctx, oid_t *oid);


int rofs_close(struct rofs_ctx *ctx, oid_t *oid);


int rofs_read(struct rofs_ctx *ctx, oid_t *oid, off_t offs, char *buff, size_t len);


int rofs_write(struct rofs_ctx *ctx, oid_t *oid, off_t offs, const char *buff, size_t len);


int rofs_truncate(struct rofs_ctx *ctx, oid_t *oid, size_t size);


int rofs_create(struct rofs_ctx *ctx, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev);


int rofs_destroy(struct rofs_ctx *ctx, oid_t *oid);


int rofs_setattr(struct rofs_ctx *ctx, oid_t *oid, int type, long long attr, const void *data, size_t size);


int rofs_getattr(struct rofs_ctx *ctx, oid_t *oid, int type, long long *attr);


int rofs_getattrall(struct rofs_ctx *ctx, oid_t *oid, struct _attrAll *attrs, size_t attrSize);


int rofs_lookup(struct rofs_ctx *ctx, oid_t *dir, const char *name, oid_t *res, oid_t *dev);


int rofs_link(struct rofs_ctx *ctx, oid_t *dir, const char *name, oid_t *oid);


int rofs_unlink(struct rofs_ctx *ctx, oid_t *dir, const char *name);


int rofs_readdir(struct rofs_ctx *ctx, oid_t *dir, off_t offs, struct dirent *dent, size_t size);


int rofs_createMapped(struct rofs_ctx *ctx, oid_t *dir, const char *name, void *addr, size_t size, oid_t *oid);


int rofs_statfs(struct rofs_ctx *ctx, void *buf, size_t len);


int rofs_devctl(struct rofs_ctx *ctx, msg_t *msg);


int rofs_mount(struct rofs_ctx *ctx, oid_t *oid, mount_i_msg_t *imnt, mount_o_msg_t *omnt);


int rofs_unmount(struct rofs_ctx *ctx);


#endif /* end of ROFS_H */
