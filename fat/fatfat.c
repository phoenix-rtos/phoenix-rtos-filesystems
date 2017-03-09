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

	if (info->type == FAT32) {
		if (cluster == 0xfffffff)
			cluster = FAT_EOF;
		if (cluster == 0xffffff8)
			cluster = FAT_EOF;
	} else if (info->type == FAT16) {
		if (cluster == 0xffff)
			cluster = FAT_EOF;
	} else { /* FAT12 */
		
	}
	//TODO more values possible here
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

	c->areas[0].start = 0;

	FATDEBUG("fatfat_lookup of cluster %d/%d\n", c->start, info->clusters);

	if (c->start >= info->clusters)
		return ERR_NOENT;
	
	if (c->start == 0) {
		FATDEBUG("fatfat_lookup of rootdir\n");
		if (info->type == FAT32) {
			c->start = info->bsbpb.fat32.BPB_RootClus;
			FATDEBUG("fatfat_lookup start is %d\n", c->start);
		} else {
			c->start = FAT_EOF;
			c->areas[0].start = info->rootoff;
			c->areas[0].size  = info->dataoff - info->rootoff;
			c->areas[1].start = 0;
			return ERR_NONE;
		}
	}

	c->areas[i].start = info->dataoff + (c->start - 2) * info->bsbpb.BPB_SecPerClus;
	c->areas[i].size = info->bsbpb.BPB_SecPerClus;
	i++;

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

			c->areas[i].start = info->dataoff + (next - 2) * info->bsbpb.BPB_SecPerClus;
			c->areas[i].size = info->bsbpb.BPB_SecPerClus;
			i++;

			if (i == SIZE_CHAIN_AREAS) {
				c->start = next;
				break;
			}

			c->areas[i].start = 0;
		}

		c->start = next;
	}

	return ERR_NONE;
}
