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


int fatdev_read(fat_info_t *info, unsigned long off, unsigned int size, char *buff)
{
	if (fseek(info->dev, info->off + off, SEEK_SET) < 0)
		return ERR_ARG;

	if (fread(buff, size, 1, info->dev) != 1)
		return ERR_PROTO;
	return ERR_NONE;
}
