/*
 * Phoenix-RTOS
 *
 * Meterfs data types definitions
 *
 * Copyright 2017, 2018, 2020, 2021, 2023 Phoenix Systems
 * Author: Aleksander Kaminski, Hubert Buczynski, Tomasz Korniluk, Hubert Badocha
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_H_
#define _METERFS_H_

#include <sys/rb.h>
#include <stdint.h>

#define MAX_NAME_LEN 8

/* clang-format off */
enum { meterfs_allocate = 0, meterfs_resize, meterfs_info, meterfs_chiperase, meterfs_fsInfo };
/* clang-format on */


typedef struct {
	int type;
	union {
		id_t id;

		struct {
			size_t sectors;
			size_t filesz;
			size_t recordsz;
			char name[MAX_NAME_LEN + 1];
		} allocate;

		struct {
			id_t id;
			size_t filesz;
			size_t recordsz;
		} resize;
	};
} meterfs_i_devctl_t;


typedef struct {
	int err;

	struct _info {
		size_t sectors;
		size_t filesz;
		size_t recordsz;
		size_t recordcnt;
	} info;

	struct {
		size_t sz;
		size_t sectorsz;
		size_t fileLimit;
		size_t filecnt;
	} fsInfo;
} meterfs_o_devctl_t;


struct _meterfs_devCtx_t;


typedef struct {
	/* meterfs internals */
	unsigned int h1Addr;
	unsigned int hcurrAddr;
	unsigned int filecnt;

	rbtree_t nodesTree;

	/* meterfs externals - should be initialized before meterfs_init */
	size_t sz;
	size_t sectorsz;

	uint32_t offset;

	struct _meterfs_devCtx_t *devCtx;

	ssize_t (*read)(struct _meterfs_devCtx_t *devCtx, off_t offs, void *buff, size_t bufflen);
	ssize_t (*write)(struct _meterfs_devCtx_t *devCtx, off_t offs, const void *buff, size_t bufflen);
	int (*eraseSector)(struct _meterfs_devCtx_t *devCtx, off_t offs);
	void (*powerCtrl)(struct _meterfs_devCtx_t *devCtx, int state);
} meterfs_ctx_t;


int meterfs_init(meterfs_ctx_t *ctx);


int meterfs_open(id_t id, meterfs_ctx_t *ctx);


int meterfs_close(id_t id, meterfs_ctx_t *ctx);


int meterfs_lookup(const char *name, id_t *res, meterfs_ctx_t *ctx);


int meterfs_allocateFile(const char *name, size_t sectorcnt, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx);


int meterfs_resizeFile(const char *name, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx);


int meterfs_readFile(id_t id, off_t off, char *buff, size_t bufflen, meterfs_ctx_t *ctx);


int meterfs_writeFile(id_t id, const char *buff, size_t bufflen, meterfs_ctx_t *ctx);


int meterfs_devctl(const meterfs_i_devctl_t *i, meterfs_o_devctl_t *o, meterfs_ctx_t *ctx);


#endif
