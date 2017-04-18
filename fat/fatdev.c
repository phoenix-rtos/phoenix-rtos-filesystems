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
#include <stdlib.h>

#include "fatdev.h"
#include "pcache.h"


int fatdev_init(const char *devname, fat_info_t *info)
{
	int dev;
	info->dev = malloc(sizeof(pcache_t));
	if (info->dev == 0)
		return -ENOMEM;
	if ((dev = open(devname, O_RDONLY)) < 0) {
		free(info->dev);
		return -ENOENT;
	}

	if (pcache_init((pcache_t *)info->dev, 512, (void *) dev, 128 * 1024) < 0) {
		free(info->dev);
		close(dev);
		return -ENOMEM;
	}
	return EOK;
}


int pcache_devread(void *dev, offs_t off, unsigned int size, char *buff)
{
	if (lseek((int) dev, off, SEEK_SET) < 0)
		return -EINVAL;

	if (read((int) dev, buff, size) != size)
		return -EPROTO;
	return EOK;
}


int fatdev_read(fat_info_t *info, offs_t off, unsigned int size, char *buff)
{
	return pcache_read(info->dev, info->off + off, size, buff);
}


void fatdev_deinit(fat_info_t *info)
{
	int dev;
	pcache_resize(info->dev, 0, (void **) &dev);
	close(dev);
	free(info->dev);
}



