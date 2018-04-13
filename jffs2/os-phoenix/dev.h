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

#include <sys/stat.h>

int old_valid_dev(dev_t dev);
int old_encode_dev(dev_t dev);
int new_encode_dev(dev_t dev);
dev_t old_decode_dev(u16 val);
dev_t new_decode_dev(u32 dev);

#endif /* _OS_PHOENIX_DEV_H_ */
