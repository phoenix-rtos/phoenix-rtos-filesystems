/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Messaging routines
 *
 * Copyright 2012 Phoenix Systems
 *
 * Author: Jacek Popko
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FS_PHFS_UDP_H_
#define _FS_PHFS_UDP_H_

#include "phfs.h"
#include "phfs_msg.h"

#define PHFS_DEFPORT	11520
int phfs_udp_init(phfs_priv_t *phfs, const phfs_opt_t *opt);

#endif
