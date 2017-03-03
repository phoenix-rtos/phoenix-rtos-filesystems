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

#ifndef _MISC_FATDEV_H_
#define _MISC_FATDEV_H_

#include "fat.h"


#define SIZE_SECTOR 512


extern int fatdev_read(fat_info_t *info, unsigned long sector, unsigned int cnt, char *buff);


#endif
