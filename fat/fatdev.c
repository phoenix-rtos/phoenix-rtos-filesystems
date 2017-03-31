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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "fatdev.h"
#include "pcache.h"


int fatdev_init(const char *devname, fat_info_t *info)
{
	if ((info->dev = open(devname, O_RDONLY)) < 0)
		return -ENOENT;

	pcache_init((void *) info->dev);
	return EOK;
}


int pcache_devread(void *dev, unsigned long off, unsigned int size, char *buff)
{
	if (lseek((int) dev, off, SEEK_SET) < 0)
		return -EINVAL;

	if (read((int) dev, buff, size) != size)
		return -EPROTO;
	return EOK;
}


int fatdev_read(fat_info_t *info, unsigned long off, unsigned int size, char *buff)
{
	return pcache_read(info->off + off, size, buff);
}


void fatdev_deinit(fat_info_t *info)
{
	close(info->dev);
}



