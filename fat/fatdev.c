/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * Hardware interface
 *
 * Copyright 2012, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Jacek Maksymowicz
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


int fatdev_read(fat_info_t *info, offs_t off, size_t size, void *buff)
{
	storage_t *strg = info->strg;
	offs_t offs = info->strg->start + off;
	ssize_t size_ret = strg->dev->blk->ops->read(strg, offs, buff, size);
	if (size_ret != size) {
		if (size_ret < 0) {
			return size_ret;
		}
		else {
			return -EIO;
		}
	}

	if (FATFS_DEBUG && (size > 4)) {
		fprintf(stderr, "FATFS dev_read %llx %llx\n", (uint64_t)off, (uint64_t)size);
	}

	return EOK;
}
