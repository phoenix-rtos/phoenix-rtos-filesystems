/*
 * Phoenix-RTOS
 *
 * Meterfs data types definitions
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_H_
#define _METERFS_H_

#include <arch.h>

enum { meterfs_allocate = 0, meterfs_resize, meterfs_info, meterfs_chiperase };

typedef struct {
	int type;
	union {
		oid_t oid;

		struct {
			size_t sectors;
			size_t filesz;
			size_t recordsz;
			char name[8];
		} allocate;

		struct {
			oid_t oid;
			size_t filesz;
			size_t recordsz;
		} resize;
	};
} meterfs_i_devctl_t;


typedef struct {
	int err;
	struct {
		size_t sectors;
		size_t filesz;
		size_t recordsz;
		size_t recordcnt;
	} info;
} meterfs_o_devctl_t;

#endif
