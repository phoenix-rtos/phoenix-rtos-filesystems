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


#include "fatio.h"


#define SIZE_SECTOR 512


extern void fatdev_init(int dev);


extern int fatdev_read(fat_info_t *info, unsigned long off, unsigned int size, char *buff);


#endif
