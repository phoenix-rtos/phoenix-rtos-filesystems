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
	enum { otDir = 0, otFile, otChrdev } type;

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
	atType
};

#endif
