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

#ifndef _MISC_FATFAT_H_
#define _MISC_FATFAT_H_

#include "fat.h"

#define SIZE_CHAIN_AREAS  8

#define FAT_EOF 0x0fffffff


typedef struct _fatfat_chain_t {
	unsigned int start;

	struct {
		unsigned int start;
		unsigned int size;
	} areas[SIZE_CHAIN_AREAS];
} fatfat_chain_t;


extern int fatfat_get(fat_info_t *info, unsigned int cluster, unsigned int *next);


extern int fatfat_set(fat_info_t *info, unsigned int cluster, unsigned int value);


extern int fatfat_lookup(fat_info_t *info, fatfat_chain_t *chain);


#endif
