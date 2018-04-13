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

#include "../os-phoenix.h"
#include "dev.h"

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

dev_t old_decode_dev(u16 val)
{
	return 0;
}

dev_t new_decode_dev(u32 dev)
{
	return 0;
}
