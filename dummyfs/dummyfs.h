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
#include <sys/file.h>
#include <posix/idtree.h>


#define DUMMYFS_SIZE_MAX 32 * 1024 * 1024

typedef uint32_t u32;

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
	uint32_t type;

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


struct _dummyfs_common_t {
	int port;
	handle_t mutex;
	int size;
};


extern struct _dummyfs_common_t dummyfs_common;


static inline int dummyfs_incsz(int size) {
	if (dummyfs_common.size + size > DUMMYFS_SIZE_MAX)
		return -ENOMEM;
	dummyfs_common.size += size;
	return EOK;
}


static inline void dummyfs_decsz(int size) {
	dummyfs_common.size -= size;
}


#endif
