/*
 * Phoenix-RTOS
 *
 * Meterfs data types definitions
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Aleksander Kaminski, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_H_
#define _METERFS_H_

#include <sys/rb.h>

#define MAX_NAME_LEN	8


enum { meterfs_allocate = 0, meterfs_resize, meterfs_info, meterfs_chiperase };


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
} meterfs_o_devctl_t;


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

	ssize_t (*read)(unsigned int addr, void *buff, size_t bufflen);
	ssize_t (*write)(unsigned int addr, void *buff, size_t bufflen);
	void (*eraseSector)(unsigned int addr);
	void (*partitionErase)(void);
	void (*powerCtrl)(int state);
} meterfs_ctx_t;


int meterfs_init(meterfs_ctx_t *ctx);


int meterfs_open(id_t id, meterfs_ctx_t *ctx);


int meterfs_close(id_t id, meterfs_ctx_t *ctx);


int meterfs_lookup(const char *name, id_t *res, meterfs_ctx_t *ctx);


int meterfs_allocateFile(const char *name, size_t sectorcnt, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx);


int meterfs_resizeFile(const char *name, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx);


int meterfs_readFile(id_t id, off_t off, char *buff, size_t bufflen, meterfs_ctx_t *ctx);


int meterfs_writeFile(id_t id, const char *buff, size_t bufflen, meterfs_ctx_t *ctx);


int meterfs_devctl(meterfs_i_devctl_t *i, meterfs_o_devctl_t *o, meterfs_ctx_t *ctx);


#endif
