/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Dummy filesystem (used before regular filesystem mounting)
 *
 * Copyright 2012 Phoenix Systems
 * Copyright 2008 Pawel Pisarczyk
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FS_DUMMYFS_H_
#define _FS_DUMMYFS_H_

#include <hal/if.h>
#include <fs/if.h>


#define SIZE_DUMMYFS_NAME   256


/* Function initializes and registers dummy filesystem */
int dummyfs_init(void);


#endif
