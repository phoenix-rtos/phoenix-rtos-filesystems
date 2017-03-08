/*
 * Phoenix-RTOS
 *
 * Misc. utilities - FAT
 *
 * Allocation table operations
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>

#include "fat.h"
#include "fatdev.h"
#include "fatfat.h"


int fatfat_get(fat_info_t *info, unsigned int cluster, unsigned int *next)
{
	unsigned int bitoff, sec, secoff;
	char sector[SIZE_SECTOR];

	if (cluster >= info->clusters)
		return ERR_ARG;
	
	if (info->type == FAT32) {
		bitoff = cluster * 32;
	} else if (info->type == FAT16)
		bitoff = cluster * 16;
	else
		bitoff = cluster * 12;

	sec = (bitoff / 8 / info->bsbpb.BPB_BytesPerSec);
	secoff = (bitoff / 8) % info->bsbpb.BPB_BytesPerSec;

	bitoff %= 8;

	if (fatdev_read(info, info->fatoff + sec, 1, sector) < 0)
		return 0;
	
	if (info->type == FAT32)
		cluster = (unsigned int)*((u32 *)&sector[secoff]);

	else if (info->type == FAT16)
		cluster = (unsigned int)*((u16 *)&sector[secoff]);

	/* FAT12 */
	else {
		if (secoff != info->bsbpb.BPB_BytesPerSec - 1)
			cluster = (unsigned int)*((u16 *)&sector[secoff]);
		else {
			cluster = ((unsigned int)*((u8 *)&sector[secoff])) << 8;
			
			if (fatdev_read(info, info->fatoff + sec + 1, 1, sector) < 0)
				return 0;

			cluster |= sector[0];
			cluster = (cluster >> (4 - bitoff)) & ~(0xf0 << bitoff);
		}
	}

	if (cluster == 0xffff)
		cluster = FAT_EOF;
	*next = cluster;
	return ERR_NONE;
}


int fatfat_set(fat_info_t *info, unsigned int cluster, unsigned int next)
{
	return 0;
}


int fatfat_lookup(fat_info_t *info, fatfat_chain_t *c)
{
	unsigned int i = 0;
	unsigned int next;

	c->areas[0].start = 1026;
	
	if (c->start >= info->clusters)
		return ERR_NOENT;

	c->areas[i].start = c->start * info->bsbpb.BPB_SecPerClus;
	c->areas[i].size = info->bsbpb.BPB_SecPerClus;

	for (;;) {

		if (fatfat_get(info, c->start, &next) < 0)
			return ERR_ARG;

		if (next == FAT_EOF) {
			c->start = next;
			break;
		}

		if (next == c->start + 1)
			c->areas[i].size += info->bsbpb.BPB_SecPerClus;
		else {
			if (i == SIZE_CHAIN_AREAS) {
				c->start = next;
				break;
			}

			c->areas[++i].start = next * info->bsbpb.BPB_SecPerClus;
			c->areas[i].size = info->bsbpb.BPB_SecPerClus;
		}

		c->start = next;
	}

	return ERR_NONE;
}
