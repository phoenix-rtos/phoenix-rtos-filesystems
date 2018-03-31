/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Remote PHoenix FileSystem
 *
 * Copyright 2012 Phoenix Systems
 * Copyright 2005-2006, 2008 Pawel Pisarczyk
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FS_PHFS_H_
#define _FS_PHFS_H_

#include <hal/if.h>
#include <fs/if.h>


/* Message types */
#define PHFS_OPEN    1
#define PHFS_READ    2
#define PHFS_WRITE   3
#define PHFS_CLOSE   4
#define PHFS_RESET   5
#define PHFS_FSTAT   6
#define PHFS_HELLO   7


typedef struct _phfs_opt_t {
	u32 magic;

	enum {
		PHFS_SERIAL,
		PHFS_UDP
	} transport;

	vnode_t* dev_vnode;
	union {
		const char *device;
		struct {
			u32 ipaddr;
			u16	port;
		};
	};
} phfs_opt_t;


/* Function initializes and registers PHFS filesystem */
int phfs_init(void);


/* Function starts thread which automatically mounts (on /net) PHFS shares available in the local network */
void phfs_automounter(void);

int phfs_connect(u32 ip, u16 port, const char* dirname);

#endif
