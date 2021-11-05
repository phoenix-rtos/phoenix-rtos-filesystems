/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2012, 2018 Phoenix Systems
 * Copyright 2008 Pawel Pisarczyk
 * Author: Pawel Pisarczyk, Kamil Amanowicz, Maciej
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DUMMYFS_INTERNAL_H_
#define _DUMMYFS_INTERNAL_H_

#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/file.h>
#include <posix/idtree.h>

#define DUMMYFS_SIZE_MAX 32 * 1024 * 1024

/* threshold for cleaning directory from deleted dirents */
#define DUMMYFS_DIRTY_DIR_AUTOCLEANUP_THRESH 8


typedef struct _dummyfs_dirent_t {
	char *name;
	unsigned int len;
	uint32_t type;
	oid_t oid;
	uint8_t deleted;

	struct _dummyfs_dirent_t *next;
	struct _dummyfs_dirent_t *prev;
} dummyfs_dirent_t;


typedef struct _dummyfs_chunk_t {
	char *data;

	offs_t offs;
	size_t size;
	size_t used;

	struct _dummyfs_chunk_t *next;
	struct _dummyfs_chunk_t *prev;
} dummyfs_chunk_t;


typedef struct _dummyfs_object_t {
	oid_t oid, dev;

	unsigned int uid;
	unsigned int gid;
	uint32_t mode;

	int refs;
	int nlink;

	idnode_t node;
	size_t size;

	union {
		dummyfs_dirent_t *entries;
		dummyfs_chunk_t *chunks;
		uint32_t port;
	};

	time_t atime;
	time_t mtime;
	time_t ctime;

	uint8_t dirty;
} dummyfs_object_t;


typedef struct {
	uint32_t port;
	handle_t mutex;
	int size;
	idtree_t dummytree;
	handle_t olock;
	rbtree_t devtree;
	handle_t devlock;
	char *mountpt;
} dummyfs_t;

int dummyfs_incsz(dummyfs_t *ctx, int size);

void dummyfs_decsz(dummyfs_t *ctx, int size);

int dummyfs_destroy(dummyfs_t *ctx, oid_t *oid);

#endif /* _DUMMYFS_INTERNAL_H_ */
