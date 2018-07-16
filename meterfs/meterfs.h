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

enum { meterfs_allocate = 0, meterfs_resize, meterfs_chiperase };

typedef struct {
	int type;
	union {
		struct {
			size_t sectors;
			size_t filesz;
			size_t recordsz;
		} allocate;

		struct {
			size_t filesz;
			size_t recordsz;
		} resize;
	};
} meterfs_i_devctl_t;


typedef struct {
	int err;
} meterfs_o_devctl_t;

#endif
