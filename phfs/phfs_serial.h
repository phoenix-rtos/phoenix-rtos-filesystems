/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Messaging routines
 *
 * Copyright 2012 Phoenix Systems
 * Copyright 2001, 2005-2006 Pawel Pisarczyk
 *
 * Author: Pawel Pisarczyk, Jacek Popko
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FS_PHFS_SERIAL_H_
#define _FS_PHFS_SERIAL_H_

#include "phfs.h"
#include "phfs_msg.h"

int phfs_serial_init(phfs_priv_t *phfs, const phfs_opt_t *opt);

#endif
