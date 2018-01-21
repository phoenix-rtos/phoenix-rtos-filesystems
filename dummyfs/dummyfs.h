/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs
 *
 * Copyright 2012, 2018 Phoenix Systems
 * Copyright 2008 Pawel Pisarczyk
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DUMMYFS_H_
#define _DUMMYFS_H_

#include <sys/rb.h>

#define DUMMYFS_SIZE_MAX 4 * 1024 * 1024

typedef struct _dummyfs_dirent_t {
	char *name;
	unsigned int len;
	oid_t oid;

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
	oid_t oid;
	enum { otDir = 0, otFile, otDev } type;

	unsigned int uid;
	unsigned int gid;
	u32 mode;

	int refs;

	size_t lmaxgap;
	size_t rmaxgap;
	rbnode_t node;

	union {
		dummyfs_dirent_t *entries;
		struct {
			size_t size;
			dummyfs_chunk_t *chunks;
		};
		u32 port;
	};

} dummyfs_object_t;


/* attribute types */
enum {
	atMode = 0,
	atUid,
	atGid,
	atSize,
	atType,
	atPort
};


struct _dummyfs_common_t{
	u32 port;
	handle_t mutex;
	int size;
};


extern struct _dummyfs_common_t dummyfs_common;


static inline int dummyfs_cksz(int size) {

	if (dummyfs_common.size + size > DUMMYFS_SIZE_MAX)
		return -ENOMEM;
	return EOK;
}


static inline void dummyfs_incsz(int size) {
	dummyfs_common.size += size;
}


static inline void dummyfs_decsz(int size) {
	dummyfs_common.size -= size;
}


#endif
