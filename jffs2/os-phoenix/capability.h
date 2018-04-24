/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _OS_PHOENIX_CAPABILITY_H_
#define _OS_PHOENIX_CAPABILITY_H_

#define CAP_SYS_RESOURCE     24

static inline bool capable(int cap)
{
	return true;
}

#endif /* _OS_PHOENIX_CAPABILITY_H_ */

