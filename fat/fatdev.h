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


extern int fatdev_init(const char *devname, fat_opt_t *opt, fat_info_t *info);


extern int fatdev_read(fat_info_t *info, offs_t off, unsigned int size, char *buff);


extern void fatdev_deinit(fat_info_t *info);


#endif
