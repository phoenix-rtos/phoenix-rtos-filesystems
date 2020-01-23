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

#define DUMMYFS_ROOT_ID 0
#define DUMMYFS_FAKE_MOUNT_ID (-1)

struct _dummyfs_object_t;


typedef struct _dummyfs_dirent {
	struct _dummyfs_dirent *next;
	struct _dummyfs_dirent *prev;

	struct _dummyfs_object *o;
	char *name;
	size_t len;
	uint8_t deleted;
} dummyfs_dirent_t;


typedef struct _dummyfs_chunk {
	offs_t offs;

	union {
		struct {
			char *data;
			struct _dummyfs_chunk *next;
			struct _dummyfs_chunk *prev;
		};
	};
	size_t size;
	size_t used;

} dummyfs_chunk_t;


typedef struct _dummyfs_object {
	idnode_t node;

	union {
		dummyfs_dirent_t *entries;
		dummyfs_chunk_t *chunks;
	};

	id_t id;
	union {
		oid_t mnt;
		oid_t dev;
	};

	time_t atime;
	time_t mtime;
	time_t ctime;

	uint32_t uid;
	uint32_t gid;
	uint32_t mode;

	int32_t refs;
	int32_t nlink;

	size_t size;
	uint8_t flags;
} dummyfs_object_t;


struct dummyfs_common {
	int portfd;
	id_t rootId;
	handle_t mutex;
#if  DUMMYFS_SIZE_CHECK == 1
	size_t size;
#endif
};


extern struct dummyfs_common dummyfs_common;


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


#define DUMMYFS_FL_DIRTY 0x1
#define DUMMYFS_FL_MOUNT 0x2
#define DUMMYFS_FL_MNTPOINT 0x4

#define DUMMYFS_GEN_OBJ_FL_FUNCS(NAME) \
	__attribute__((always_inline)) inline int OBJ_IS_##NAME(dummyfs_object_t *o) \
			{ return o->flags & DUMMYFS_FL_##NAME; } \
	__attribute__((always_inline)) inline int OBJ_SET_##NAME(dummyfs_object_t *o) \
			{ return o->flags |= DUMMYFS_FL_##NAME; } \
	__attribute__((always_inline)) inline int OBJ_CLR_##NAME(dummyfs_object_t *o) \
			{ return o->flags &= ~DUMMYFS_FL_##NAME; }

DUMMYFS_GEN_OBJ_FL_FUNCS(DIRTY)
DUMMYFS_GEN_OBJ_FL_FUNCS(MOUNT)
DUMMYFS_GEN_OBJ_FL_FUNCS(MNTPOINT)

#endif
