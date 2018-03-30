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


#ifndef _OS_PHOENIX_DEV_H_
#define _OS_PHOENIX_DEV_H_


typedef u32 dev_t;


int old_valid_dev(dev_t dev)
{
	return 0;
}

int old_encode_dev(dev_t dev)
{
	return 0;
}

int new_encode_dev(dev_t dev)
{
	return 0;
}


#endif /* _OS_PHOENIX_DEV_H_ */
