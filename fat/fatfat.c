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


#include "fatdev.h"
#include "fatfat.h"


int fatfat_get(fat_info_t *info, unsigned int cluster, unsigned int *next)
{
	unsigned int bitoff, sec, secoff;
	char sector[SIZE_SECTOR];

	if (cluster >= info->clusters)
		return -EINVAL;
	
	if (info->type == FAT32) {
		bitoff = cluster * 32;
	} else if (info->type == FAT16)
		bitoff = cluster * 16;
	else
		bitoff = cluster * 12;

	sec = (bitoff / 8 / info->bsbpb.BPB_BytesPerSec);
	secoff = (bitoff / 8) % info->bsbpb.BPB_BytesPerSec;

	bitoff %= 8;

	if (fatdev_read(info, (info->fatoff + sec) * info->bsbpb.BPB_BytesPerSec, info->bsbpb.BPB_BytesPerSec, sector) < 0)
		return 0;
	
	if (info->type == FAT32)
		cluster = (unsigned int)(((u32 *)&sector)[secoff / sizeof(u32)]);

	else if (info->type == FAT16)
		cluster = (unsigned int)(((u16 *)&sector)[secoff / sizeof(u16)]);

	/* FAT12 */
	else {
		if (secoff != info->bsbpb.BPB_BytesPerSec - 1)
			cluster = (unsigned int)(((u16 *)&sector)[secoff / sizeof(u16)]);
		else {
			cluster = ((unsigned int)*((u8 *)&sector[secoff])) << 8;
			
			if (fatdev_read(info, (info->fatoff + sec + 1) * info->bsbpb.BPB_BytesPerSec, info->bsbpb.BPB_BytesPerSec, sector) < 0)
				return 0;

			cluster |= sector[0];
			cluster = (cluster >> (4 - bitoff)) & ~(0xf0 << bitoff);
		}
	}

	if (info->type == FAT32) {
		if (cluster >= 0xffffff8)
			cluster = FAT_EOF;
	} else if (info->type == FAT16) {
		if (cluster >= 0xfff8)
			cluster = FAT_EOF;
	} else { /* FAT12 */
		if (cluster >= 0xff8)
			cluster = FAT_EOF;
	}
	*next = cluster;
	return EOK;
}


int fatfat_set(fat_info_t *info, unsigned int cluster, unsigned int next)
{
	return 0;
}

#include <assert.h>
int fatfat_lookup(fat_info_t *info, fatfat_chain_t *c, unsigned int skip)
{
	unsigned int i = 0;
	unsigned int next;

	c->areas[0].start = 0;

	assert (c->start != FAT_EOF);
	if (c->start >= info->clusters)
		return -ENOENT;
	
	if (c->start == 0) {
		if (info->type == FAT32) {
			c->start = info->bsbpb.fat32.BPB_RootClus;
		} else {
			c->start = FAT_EOF;
			if ((skip > 0) && (skip >= c->areas[0].size)) {
				c->soff = 0;
				c->scnt = 0;
			} else {
				c->areas[0].start = info->rootoff + skip;
				c->areas[0].size  = info->dataoff - info->rootoff - skip;
				c->areas[1].start = 0;
				c->soff = skip;
				c->scnt = c->areas[0].size;
			}
			return EOK;
		}
	}

	c->areas[i].start = info->dataoff + (c->start - 2) * info->bsbpb.BPB_SecPerClus;
	c->areas[i].size = info->bsbpb.BPB_SecPerClus;
	c->areas[i + 1].start = 0;
	c->soff += c->scnt + skip;
	c->scnt = 0;

	for (;;) {
		if (fatfat_get(info, c->start, &next) < 0)
			return -EINVAL;

		if (next == FAT_EOF) {
			if (skip >= c->areas[i].size) {
				c->areas[i].start = 0;
			} else {
				c->areas[i].size  -= skip;
				c->areas[i].start += skip;
				c->scnt += c->areas[i].size;
			}
			c->start = next;
			break;
		}

		if (next == c->start + 1)
			c->areas[i].size += info->bsbpb.BPB_SecPerClus;
		else {
			if (skip > 0) {
				if (skip < c->areas[i].size) {
					c->areas[i].size  -= skip;
					c->areas[i].start += skip;
					c->scnt += c->areas[i].size;
					skip = 0;
					i++;
				} else {
					skip -= c->areas[i].size;
				}
			} else {
				c->scnt += c->areas[i].size;
				i++;
			}

			if (i == SIZE_CHAIN_AREAS) {
				c->start = next;
				break;
			}
			c->areas[i].start = info->dataoff + (next - 2) * info->bsbpb.BPB_SecPerClus;
			c->areas[i].size = info->bsbpb.BPB_SecPerClus;
			if (i < SIZE_CHAIN_AREAS - 1)
				c->areas[i + 1].start = 0;
		}

		c->start = next;
	}

	return EOK;
}
