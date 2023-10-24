/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * Hardware interface header file
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FATDEV_H_
#define _FATDEV_H_


#include "fatio.h"

extern int fatdev_read(fat_info_t *info, offs_t off, size_t size, void *buff);


#endif /* _FATDEV_H_ */
