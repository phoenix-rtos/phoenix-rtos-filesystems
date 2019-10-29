/*
 * Phoenix-RTOS
 *
 * dummyfs
 *
 * Copyright 2012, 2018 Phoenix Systems
 * Copyright 2008 Pawel Pisarczyk
 * Author: Pawel Pisarczyk, Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DUMMYFS_H_
#define _DUMMYFS_H_

#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/file.h>
#include <posix/idtree.h>


#define DUMMYFS_SIZE_CHECK 1
#define DUMMYFS_SIZE_MAX 32 * 1024 * 1024


struct _dummyfs_object_t;


typedef struct _dummyfs_dirent_t {
	struct _dummyfs_dirent_t *next;
	struct _dummyfs_dirent_t *prev;

	struct _dummyfs_object_t *o;
	char *name;
	size_t len;
	uint8_t deleted;
} dummyfs_dirent_t;


typedef struct _dummyfs_chunk_t {
	offs_t offs;

	char *data;
	struct _dummyfs_chunk_t *next;
	struct _dummyfs_chunk_t *prev;

	size_t size;
	size_t used;
} dummyfs_chunk_t;


typedef struct _dummyfs_object_t {
	idnode_t node;

	union {
		struct {
			dummyfs_dirent_t *entries;
			uint8_t dirty;
		};
		dummyfs_chunk_t *chunks;
		oid_t dev;
	};

	id_t id;

	time_t atime;
	time_t mtime;
	time_t ctime;

	uint32_t uid;
	uint32_t gid;
	uint32_t mode;

	int32_t refs;
	int32_t nlink;

	size_t size;
} dummyfs_object_t;


struct _dummyfs_common_t {
	int portfd;
	id_t rootId;
	handle_t mutex;
#if  DUMMYFS_SIZE_CHECK == 1
	size_t size;
#endif
};


extern struct _dummyfs_common_t dummyfs_common;


static inline int dummyfs_incsz(size_t size) {
#if  DUMMYFS_SIZE_CHECK == 1
	if (dummyfs_common.size + size > DUMMYFS_SIZE_MAX)
		return -ENOMEM;
	dummyfs_common.size += size;
#endif
	return EOK;
}


static inline void dummyfs_decsz(size_t size) {
#if DUMMYFS_SIZE_CHECK == 1
	if (dummyfs_common.size >= size)
		dummyfs_common.size -= size;
	else
		dummyfs_common.size = 0;
#endif
}


#endif
