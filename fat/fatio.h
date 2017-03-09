/*
 * Phoenix-RTOS
 *
 * Misc. FAT
 *
 * FAT implementation
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _MISC_FATIO_H_
#define _MISC_FATIO_H_

#include "fat.h"


typedef struct _fat_name_t {
	char name[12];
	u8 len;
} __attribute__((packed)) fat_name_t;


extern int fatio_lookup(fat_info_t *info, const char *path, fat_dirent_t *d);


extern int fatio_readsuper(void *opt, fat_info_t **out);


#endif
