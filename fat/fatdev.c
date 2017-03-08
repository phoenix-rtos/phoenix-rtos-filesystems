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

#include "fatdev.h"


int fatdev_read(fat_info_t *info, unsigned long sector, unsigned int cnt, char *buff) 
{
	unsigned long off;
	int l;

	if (fseek(info->dev, (info->off + sector) * SIZE_SECTOR, SEEK_SET) < 0)
		return ERR_ARG;

	for (off = 0; off < cnt * SIZE_SECTOR;) {

		if ((l = fread(buff + off, SIZE_SECTOR, cnt, info->dev)) < 0)
			return ERR_PROTO;

		off += (l * SIZE_SECTOR);
	}

	return ERR_NONE;
}
