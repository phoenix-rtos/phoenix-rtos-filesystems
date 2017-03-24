/*
 * Phoenix-RTOS
 *
 * Misc. FAT
 *
 * Device support
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "fatdev.h"
#include "pcache.h"


void fatdev_init(int dev)
{
	pcache_init(dev);
}


int fatdev_read(fat_info_t *info, unsigned long off, unsigned int size, char *buff)
{
	return pcache_read(info->off + off, size, buff);
}

